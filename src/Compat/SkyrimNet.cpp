#include "SD/Compat/SkyrimNet.h"

#include "SD/Core/Logging.h"

#include <deque>

namespace SD::Compat
{
	namespace
	{
		// The three exports this uses, with the signatures from SkyrimNet's
		// PublicAPI.h. Only these are resolved; the rest of the API is left alone.
		using GetVersionFn = int (*)();
		using RegisterEventCallbackFn = std::uint64_t (*)(const char*, std::function<void(const char*)>);

		// Event callbacks arrived in API v5.
		constexpr int kMinimumApiVersion = 5;

		// A conversation never has this many lines in flight; anything beyond it is a
		// backlog from a stalled frame source and is dropped from the front.
		constexpr std::size_t kQueueLimit = 32;

		std::atomic_bool installed{ false };
		std::atomic_bool listening{ false };
		std::atomic_int  apiVersion{ 0 };
		std::atomic_bool accepting{ false };

		std::mutex                           queueLock;
		std::deque<SkyrimNetEvent::Line>     queue;

		Log::OnceFlag parseFailureReported;
		Log::OnceFlag overflowReported;

		void Receive(std::string_view a_type, const char* a_json) noexcept
		{
			// Nothing may escape into SkyrimNet's thread pool.
			try {
				if (!accepting.load(std::memory_order_relaxed) || !a_json) {
					return;
				}

				// Copied before parsing: the pointer is only valid for this call.
				const std::string json{ a_json };
				auto              line = SkyrimNetEvent::Parse(a_type, json);
				if (!line) {
					if (parseFailureReported.Take()) {
						Log::Warn(Log::Category::kDialogue,
							"SkyrimNet: a '{}' event couldn't be read; it and any like it are skipped. "
							"First 200 characters: {}"sv,
							a_type, json.substr(0, 200));
					}
					return;
				}

				Log::Info(Log::Category::kDialogue,
					"SkyrimNet {}: [{:08X}] -> [{:08X}], {} characters."sv,
					a_type, line->speaker, line->target, line->text.size());

				const std::scoped_lock lock{ queueLock };
				if (queue.size() >= kQueueLimit) {
					queue.pop_front();
					if (overflowReported.Take()) {
						Log::Warn(Log::Category::kDialogue,
							"SkyrimNet: more than {} lines waiting; the oldest are being dropped."sv, kQueueLimit);
					}
				}
				queue.push_back(std::move(*line));
			} catch (...) {
				// A failed allocation or log write loses one line, nothing more.
			}
		}

		// Plain functions, not capturing lambdas: they fit in std::function's small
		// buffer, so handing them to SkyrimNet allocates nothing across the boundary.
		void OnNpcLine(const char* a_json) { Receive("dialogue"sv, a_json); }
		void OnPlayerText(const char* a_json) { Receive("dialogue_player_text"sv, a_json); }
		void OnPlayerSpeech(const char* a_json) { Receive("dialogue_player_stt"sv, a_json); }
	}

	void SkyrimNet::Connect()
	{
		if (listening.load(std::memory_order_relaxed)) {
			return;
		}

		// SKSE has loaded every plugin by kDataLoaded, so SkyrimNet is either in the
		// process already or not installed. Never LoadLibrary it from here: an SKSE
		// plugin loaded outside SKSE wouldn't be initialised.
		HMODULE module = ::GetModuleHandleW(L"SkyrimNet.dll");
		if (!module) {
			Log::Info(Log::Category::kCompat,
				"SkyrimNet not installed; the SkyrimNet cinematic mode stays idle."sv);
			return;
		}
		installed.store(true, std::memory_order_relaxed);

		const auto getVersion = reinterpret_cast<GetVersionFn>(::GetProcAddress(module, "PublicGetVersion"));
		if (!getVersion) {
			Log::Warn(Log::Category::kCompat,
				"SkyrimNet is loaded but exports no PublicGetVersion; the SkyrimNet cinematic mode stays idle."sv);
			return;
		}

		const int version = getVersion();
		apiVersion.store(version, std::memory_order_relaxed);
		if (version < kMinimumApiVersion) {
			Log::Warn(Log::Category::kCompat,
				"SkyrimNet API v{} is older than v{}, which added the dialogue events this needs. "
				"Update SkyrimNet to use the SkyrimNet cinematic mode."sv,
				version, kMinimumApiVersion);
			return;
		}

		const auto registerCallback = reinterpret_cast<RegisterEventCallbackFn>(
			::GetProcAddress(module, "PublicRegisterEventCallback"));
		if (!registerCallback) {
			Log::Warn(Log::Category::kCompat,
				"SkyrimNet API v{} has no PublicRegisterEventCallback; the SkyrimNet cinematic mode stays idle."sv,
				version);
			return;
		}

		struct Subscription
		{
			const char* type;
			void (*callback)(const char*);
		};
		constexpr std::array<Subscription, 3> subscriptions{ {
			{ "dialogue", &OnNpcLine },
			{ "dialogue_player_text", &OnPlayerText },
			{ "dialogue_player_stt", &OnPlayerSpeech },
		} };

		int registered = 0;
		for (const auto& subscription : subscriptions) {
			std::uint64_t id = 0;
			try {
				id = registerCallback(subscription.type, std::function<void(const char*)>{ subscription.callback });
			} catch (...) {
				id = 0;
			}
			if (id == 0) {
				Log::Warn(Log::Category::kCompat, "SkyrimNet refused the '{}' callback."sv, subscription.type);
				continue;
			}
			++registered;
		}

		if (registered == 0) {
			Log::Warn(Log::Category::kCompat,
				"SkyrimNet API v{}: no dialogue callback could be registered; the SkyrimNet cinematic mode stays idle."sv,
				version);
			return;
		}

		listening.store(true, std::memory_order_relaxed);
		Log::Info(Log::Category::kCompat,
			"SkyrimNet API v{} found; listening to {} of 3 dialogue event types."sv, version, registered);
	}

	bool SkyrimNet::Installed() noexcept
	{
		return installed.load(std::memory_order_relaxed);
	}

	bool SkyrimNet::Listening() noexcept
	{
		return listening.load(std::memory_order_relaxed);
	}

	int SkyrimNet::ApiVersion() noexcept
	{
		return apiVersion.load(std::memory_order_relaxed);
	}

	void SkyrimNet::SetAccepting(bool a_accepting) noexcept
	{
		accepting.store(a_accepting, std::memory_order_relaxed);
	}

	std::vector<SkyrimNetEvent::Line> SkyrimNet::Drain()
	{
		std::vector<SkyrimNetEvent::Line> lines;
		const std::scoped_lock            lock{ queueLock };
		lines.reserve(queue.size());
		while (!queue.empty()) {
			lines.push_back(std::move(queue.front()));
			queue.pop_front();
		}
		return lines;
	}

	void SkyrimNet::Clear()
	{
		const std::scoped_lock lock{ queueLock };
		queue.clear();
	}
}
