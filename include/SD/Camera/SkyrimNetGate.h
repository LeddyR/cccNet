#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

namespace SD::Camera
{
	// Holds SkyrimNet lines until the person saying them can actually be heard.
	//
	// SkyrimNet reports a line when it has been written, which is often seconds
	// before its voice is ready. Cutting to the speaker then shows somebody
	// standing in silence while the previous speaker is still talking. So a line
	// waits here until its speaker is audible (a voice handle playing or a
	// subtitle up, decided by the caller), in the order SkyrimNet sent them.
	//
	// - The oldest line goes as soon as its speaker is heard.
	// - A newer line whose speaker *starts* talking (silent at some point after the
	//   line arrived, audible now) means the lines before it were never voiced, so
	//   they're dropped and it goes.
	// - The player's own lines (typed or spoken into the microphone) don't wait
	//   longer than playerWait: they were just entered.
	// - A line nobody is heard saying within npcWait seconds is dropped, once any
	//   line has ever been heard this way. Until then the voice may simply be
	//   invisible to these checks (a voice played outside the actor), and the line
	//   is used anyway so the camera still works, just without waiting for audio.
	//
	// Pure and templated on the line type, so it can be tested without the game.
	template <typename Line>
	class SkyrimNetGate
	{
	public:
		struct Waits
		{
			float npc{ 8.0f };     // seconds an NPC line may wait for its voice; 0 = never wait
			float player{ 0.75f }; // seconds a player line waits before it goes anyway
		};

		enum class Why : std::uint8_t
		{
			kNone,
			kHeard,          // its speaker is audible
			kStartedLater,   // a newer line's speaker started; older ones were dropped
			kPlayerLine,     // the player's line, after its short wait
			kNoWait,         // waiting is switched off
			kUnheard,        // nobody was heard, and nothing ever has been; used anyway
		};

		struct Result
		{
			std::optional<Line> line{};
			Why                 why{ Why::kNone };
			std::size_t         dropped{ 0 };  // lines given up on this frame
		};

		static constexpr std::size_t kCapacity = 16;

		// a_speaker is who says it, a_player whether that is the player. a_audibleNow
		// is whether that person can already be heard (someone continuing to talk).
		void Push(Line a_line, std::uint32_t a_speaker, bool a_player, bool a_audibleNow)
		{
			if (waiting.size() >= kCapacity) {
				waiting.pop_front();
				++overflowed;
			}
			waiting.push_back(Entry{ std::move(a_line), a_speaker, a_player, 0.0f, !a_audibleNow });
		}

		// One frame. a_audible(speaker) says whether that speaker is audible now.
		// Returns at most one line per frame.
		template <typename Audible>
		[[nodiscard]] Result Next(float a_delta, const Waits& a_waits, Audible&& a_audible)
		{
			Result result{};
			result.dropped = std::exchange(overflowed, std::size_t{ 0 });
			if (waiting.empty()) {
				return result;
			}

			const float delta = std::max(a_delta, 0.0f);
			heardNow.assign(waiting.size(), false);
			startedNow.assign(waiting.size(), false);
			for (std::size_t i = 0; i < waiting.size(); ++i) {
				auto& entry = waiting[i];
				entry.age += delta;
				const bool audible = a_audible(entry.speaker);
				heardNow[i] = audible;
				startedNow[i] = audible && entry.silentSeen;
				if (!audible) {
					entry.silentSeen = true;
				}
			}

			auto& head = waiting.front();

			if (a_waits.npc <= 0.0f && !head.player) {
				return Take(0, Why::kNoWait, result);
			}
			if (head.player && (heardNow[0] || head.age >= a_waits.player)) {
				return Take(0, Why::kPlayerLine, result);
			}
			if (!head.player && heardNow[0]) {
				heardEver = true;
				return Take(0, Why::kHeard, result);
			}

			// Someone further back started talking: everything before them was skipped.
			for (std::size_t i = 1; i < waiting.size(); ++i) {
				if (startedNow[i]) {
					if (!waiting[i].player) {
						heardEver = true;
					}
					return Take(i, Why::kStartedLater, result);
				}
			}

			if (!head.player && head.age >= a_waits.npc) {
				if (heardEver) {
					waiting.pop_front();
					++result.dropped;
					return result;
				}
				return Take(0, Why::kUnheard, result);
			}
			return result;
		}

		void Clear() noexcept
		{
			waiting.clear();
			overflowed = 0;
		}

		[[nodiscard]] bool        Empty() const noexcept { return waiting.empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return waiting.size(); }

		// Whether any NPC line has been matched to an audible voice. Kept across
		// Clear(): it describes the setup, not the conversation.
		[[nodiscard]] bool HeardEver() const noexcept { return heardEver; }

	private:
		struct Entry
		{
			Line          line;
			std::uint32_t speaker{ 0 };
			bool          player{ false };
			float         age{ 0.0f };

			// The speaker has been silent at least once since the line arrived, so their
			// being audible now is this line starting, not an earlier one carrying on.
			bool silentSeen{ false };
		};

		Result& Take(std::size_t a_index, Why a_why, Result& a_result)
		{
			a_result.dropped += a_index;
			a_result.line = std::move(waiting[a_index].line);
			a_result.why = a_why;
			waiting.erase(waiting.begin(), waiting.begin() + static_cast<std::ptrdiff_t>(a_index + 1));
			return a_result;
		}

		std::deque<Entry> waiting{};
		std::vector<bool> heardNow{};
		std::vector<bool> startedNow{};
		std::size_t       overflowed{ 0 };
		bool              heardEver{ false };
	};
}
