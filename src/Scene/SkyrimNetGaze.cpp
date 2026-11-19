#include "SD/Scene/SkyrimNetGaze.h"

#include "SD/Core/Logging.h"
#include "SD/Dialogue/Session.h"

namespace SD::Scene
{
	namespace
	{
		using Track = RE::HighProcessData::HEAD_TRACK_TYPES;

		// Someone whose head this has pointed, and where.
		struct Looker
		{
			RE::ActorHandle actor{};
			RE::ActorHandle target{};
		};

		RE::ActorHandle     speaker{};
		RE::ActorHandle     listener{};
		float               remaining{ 0.0f };
		float               range{ 0.0f };
		std::vector<Looker> lookers{};

		// The person spoken to looks from further than the rest: anywhere within
		// talking distance, whatever the range for bystanders.
		constexpr float kListenerReach = 1500.0f;

		// Lookers are re-gathered this often; heads are re-pointed every frame.
		constexpr float kGatherInterval = 0.25f;
		float           gatherCountdown{ 0.0f };

		[[nodiscard]] RE::HighProcessData* HighOf(RE::Actor* a_actor)
		{
			auto* process = a_actor ? a_actor->GetActorRuntimeData().currentProcess : nullptr;
			return process ? process->high : nullptr;
		}

		[[nodiscard]] float Distance(const RE::NiPoint3& a, const RE::NiPoint3& b)
		{
			const float dx = a.x - b.x;
			const float dy = a.y - b.y;
			const float dz = a.z - b.z;
			return std::sqrt(dx * dx + dy * dy + dz * dz);
		}

		// Someone who can sensibly be asked to look: loaded, alive, conscious, and not
		// busy fighting.
		[[nodiscard]] bool CanLook(RE::Actor* a_actor)
		{
			if (!a_actor || !a_actor->Is3DLoaded() || a_actor->IsDead() || a_actor->IsDisabled() ||
				a_actor->IsInCombat() || !HighOf(a_actor)) {
				return false;
			}
			const auto* state = a_actor->AsActorState();
			return !state || (!state->IsUnconscious() && !state->IsBleedingOut());
		}

		// Clear a head this pointed, if it still points there.
		void Hand(const Looker& a_looker)
		{
			auto actor = a_looker.actor.get();
			auto* high = HighOf(actor.get());
			if (!high) {
				return;
			}
			auto current = high->headTrackTarget[Track::kDialogue].get();
			auto target = a_looker.target.get();
			if (current && target && current.get() == target.get()) {
				high->SetHeadtrackTarget(Track::kDialogue, nullptr);
			}
		}

		[[nodiscard]] Looker* Find(RE::Actor* a_actor)
		{
			for (auto& looker : lookers) {
				if (looker.actor.get().get() == a_actor) {
					return &looker;
				}
			}
			return nullptr;
		}

		// Point a_actor's head at a_target, and remember that this did.
		void Point(RE::Actor* a_actor, RE::Actor* a_target)
		{
			auto* high = HighOf(a_actor);
			if (!high || !a_target) {
				return;
			}
			high->SetHeadtrackTarget(Track::kDialogue, a_target);
			if (auto* looker = Find(a_actor)) {
				looker->target = a_target->GetHandle();
			} else {
				lookers.push_back(Looker{ a_actor->GetHandle(), a_target->GetHandle() });
			}
		}

		// Who should be looking, and at whom, as handles: an actor can unload between
		// gathers.
		using Pairs = std::vector<std::pair<RE::ActorHandle, RE::ActorHandle>>;

		// The speaker looks at their listener; everyone else in range looks at the
		// speaker.
		void Gather(RE::Actor* a_speaker, RE::Actor* a_listener, Pairs& a_out)
		{
			a_out.clear();
			auto* player = static_cast<RE::Actor*>(RE::PlayerCharacter::GetSingleton());

			if (a_speaker != player && a_listener && a_listener != a_speaker && CanLook(a_speaker)) {
				a_out.emplace_back(a_speaker->GetHandle(), a_listener->GetHandle());
			}

			const auto here = a_speaker->GetPosition();
			if (a_listener && a_listener != player && a_listener != a_speaker && CanLook(a_listener) &&
				Distance(here, a_listener->GetPosition()) <= std::max(range, kListenerReach)) {
				a_out.emplace_back(a_listener->GetHandle(), a_speaker->GetHandle());
			}

			auto* lists = RE::ProcessLists::GetSingleton();
			if (!lists || range <= 0.0f) {
				return;
			}
			for (auto& handle : lists->highActorHandles) {
				auto  actor = handle.get();
				auto* raw = actor.get();
				if (!raw || raw == player || raw == a_speaker || raw == a_listener || !CanLook(raw)) {
					continue;
				}
				if (Distance(here, raw->GetPosition()) > range) {
					continue;
				}
				a_out.emplace_back(raw->GetHandle(), a_speaker->GetHandle());
			}
		}

		Pairs wanted{};
	}

	void SkyrimNetGaze::Speak(RE::Actor* a_speaker, RE::Actor* a_listener, float a_seconds, float a_range)
	{
		if (!a_speaker) {
			return;
		}
		// A different speaker: heads that only looked at the last one are handed back
		// at the next gather if they no longer belong.
		speaker = a_speaker->GetHandle();
		listener = a_listener ? a_listener->GetHandle() : RE::ActorHandle{};
		remaining = std::clamp(a_seconds, 1.0f, 60.0f);
		range = std::max(a_range, 0.0f);
		gatherCountdown = 0.0f;
	}

	void SkyrimNetGaze::Update(float a_delta)
	{
		if (remaining <= 0.0f) {
			if (!lookers.empty()) {
				Release();
			}
			return;
		}

		// The player's own conversation through the dialogue menu has its own head
		// tracking (Presence); two writers on one slot would flicker.
		if (Dialogue::Session::GetSingleton().Active()) {
			Release();
			return;
		}

		remaining -= std::max(a_delta, 0.0f);

		auto talker = speaker.get();
		if (!talker || !talker->Is3DLoaded() || talker->IsDead()) {
			Release();
			return;
		}
		auto addressed = listener.get();

		gatherCountdown -= std::max(a_delta, 0.0f);
		if (gatherCountdown <= 0.0f) {
			gatherCountdown = kGatherInterval;
			Gather(talker.get(), addressed.get(), wanted);

			// Anyone pointed earlier who no longer belongs (walked off, a new speaker
			// whose circle they're outside) gets their head back.
			for (auto it = lookers.begin(); it != lookers.end();) {
				auto actor = it->actor.get();
				const bool stillWanted = actor && std::any_of(wanted.begin(), wanted.end(),
					[&](const auto& a_pair) { return a_pair.first.get().get() == actor.get(); });
				if (!stillWanted) {
					Hand(*it);
					it = lookers.erase(it);
				} else {
					++it;
				}
			}
		}

		// Re-pointed every frame: the engine and packages write this slot too.
		for (const auto& [lookerHandle, targetHandle] : wanted) {
			auto looking = lookerHandle.get();
			auto at = targetHandle.get();
			if (looking && at && CanLook(looking.get())) {
				Point(looking.get(), at.get());
			}
		}
	}

	void SkyrimNetGaze::Release()
	{
		for (const auto& looker : lookers) {
			Hand(looker);
		}
		lookers.clear();
		wanted.clear();
		speaker = {};
		listener = {};
		remaining = 0.0f;
	}
}
