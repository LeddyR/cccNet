#include "SD/Camera/SkyrimNetGate.h"
#include "SD/Compat/SkyrimNetEvent.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

namespace
{
	void Expect(bool condition, const char* message)
	{
		if (!condition) {
			std::cerr << "SkyrimNet: " << message << '\n';
			std::exit(EXIT_FAILURE);
		}
	}

	namespace Event = SD::Compat::SkyrimNetEvent;
	using Gate = SD::Camera::SkyrimNetGate<int>;

	constexpr float kFrame = 0.1f;

	void ParseTests()
	{
		// The documented shape: data is a JSON string holding an object.
		{
			const auto line = Event::Parse("dialogue",
				R"({"type":"dialogue","data":"{\"text\":\"Well met, Dragonborn.\",\"speaker\":\"Lydia\"}",)"
				R"("originatingActorFormId":659867,"targetActorFormId":20,"id":42})");
			Expect(line.has_value(), "the documented event parses");
			Expect(line->kind == Event::Kind::kNpc, "dialogue is an NPC line");
			Expect(line->speaker == 659867u, "speaker form ID");
			Expect(line->target == 0x14u, "target form ID");
			Expect(line->text == "Well met, Dragonborn.", "text from the nested data object");
			Expect(line->id == 42u, "event id");
		}

		// data as an object, with the text under "dialogue".
		{
			const auto line = Event::Parse("dialogue",
				R"({"data":{"dialogue":"  Hello there.  "},"originatingActorFormId":4096,"targetActorFormId":8192})");
			Expect(line && line->text == "Hello there.", "text under 'dialogue', trimmed");
			Expect(line && line->target == 8192u, "object data keeps the target");
		}

		// Form IDs as strings, hex and decimal, and a negative signed light-plugin ID.
		{
			const auto hex = Event::Parse("dialogue",
				R"({"data":"Hi","originatingActorFormId":"0xFE001234","targetActorFormId":"20"})");
			Expect(hex && hex->speaker == 0xFE001234u, "hex string form ID");
			Expect(hex && hex->target == 20u, "decimal string form ID");
			Expect(hex && hex->text == "Hi", "plain-text data");

			const auto negative = Event::Parse("dialogue",
				R"({"data":"Hi","originatingActorFormId":-33549772})");
			Expect(negative && negative->speaker == 0xFE001234u, "a signed form ID is reinterpreted");
		}

		// The player's own lines: the speaker is always the player.
		{
			const auto typed = Event::Parse("dialogue_player_text",
				R"({"data":"{\"text\":\"Where are we going?\"}","originatingActorFormId":0,"targetActorFormId":659867})");
			Expect(typed && typed->kind == Event::Kind::kPlayerText, "typed line kind");
			Expect(typed && typed->speaker == Event::kPlayerFormId, "player events are the player's");
			Expect(typed && typed->target == 659867u, "player line target");

			const auto spoken = Event::Parse("dialogue_player_stt",
				R"({"data":"Follow me.","originatingActorFormId":20,"targetActorFormId":20})");
			Expect(spoken && spoken->kind == Event::Kind::kPlayerSpeech, "spoken line kind");
			Expect(spoken && spoken->target == 0u, "a player target of the player is nobody");
		}

		// Rejects.
		Expect(!Event::Parse("dialogue", "not json").has_value(), "malformed JSON");
		Expect(!Event::Parse("dialogue", "[1,2,3]").has_value(), "not an object");
		Expect(!Event::Parse("dialogue", R"({"data":"Hi"})").has_value(), "an NPC line needs a speaker");
		Expect(!Event::Parse("dialogue", R"({"data":"Hi","originatingActorFormId":20})").has_value(),
			"an NPC line from the player is not an NPC line");
		Expect(!Event::Parse("combat", R"({"originatingActorFormId":5})").has_value(), "other event types");

		// Self-addressed means nobody in particular.
		{
			const auto self = Event::Parse("dialogue", R"({"data":"Hmm.","originatingActorFormId":77,"targetActorFormId":77})");
			Expect(self && self->target == 0u, "speaking to oneself is no target");
		}

		// Overlong text is cut, not rejected.
		{
			std::string json = R"({"originatingActorFormId":77,"data":")";
			json += std::string(5000, 'a');
			json += R"("})";
			const auto longLine = Event::Parse("dialogue", json);
			Expect(longLine && longLine->text.size() == Event::kMaxText, "long text is capped");
		}
	}

	void GateTests()
	{
		const Gate::Waits waits{ 8.0f, 0.75f };

		// A line waits for its speaker's voice, then goes.
		{
			Gate gate;
			std::set<std::uint32_t> talking;
			gate.Push(1, 100, false, false);
			for (int i = 0; i < 20; ++i) {
				Expect(!gate.Next(kFrame, waits, [&](std::uint32_t s) { return talking.count(s) > 0; }).line,
					"nothing before the voice starts");
			}
			talking.insert(100);
			const auto result = gate.Next(kFrame, waits, [&](std::uint32_t s) { return talking.count(s) > 0; });
			Expect(result.line && *result.line == 1 && result.why == Gate::Why::kHeard, "goes when heard");
			Expect(gate.Empty() && gate.HeardEver(), "consumed, and the setup is known to be audible");
		}

		// Lines go in order: a newer line can't jump ahead because its speaker is still
		// talking from before.
		{
			Gate gate;
			std::set<std::uint32_t> talking{ 100 };
			const auto audible = [&](std::uint32_t s) { return talking.count(s) > 0; };
			gate.Push(1, 200, false, false);  // B's reply, voice not ready yet
			gate.Push(2, 100, false, true);   // A's next line; A still talking from before
			Expect(!gate.Next(kFrame, waits, audible).line, "A carrying on doesn't skip B");
			talking = { 200 };
			auto result = gate.Next(kFrame, waits, audible);
			Expect(result.line && *result.line == 1, "B goes when B is heard");
			talking = { 100 };
			result = gate.Next(kFrame, waits, audible);
			Expect(result.line && *result.line == 2, "then A, once A starts again");
		}

		// A line that is never voiced is skipped when someone after it starts talking.
		{
			Gate gate;
			std::set<std::uint32_t> talking;
			const auto audible = [&](std::uint32_t s) { return talking.count(s) > 0; };
			gate.Push(1, 200, false, false);
			gate.Push(2, 300, false, false);
			Expect(!gate.Next(kFrame, waits, audible).line, "nobody talking yet");
			talking = { 300 };
			const auto result = gate.Next(kFrame, waits, audible);
			Expect(result.line && *result.line == 2 && result.why == Gate::Why::kStartedLater, "the later speaker started");
			Expect(result.dropped == 1 && gate.Empty(), "the unvoiced line was dropped");
		}

		// Before any voice has ever been matched, an unheard line still goes after the
		// wait, so a setup whose voices can't be seen still gets a camera.
		{
			Gate gate;
			const auto silent = [](std::uint32_t) { return false; };
			gate.Push(7, 100, false, false);
			Gate::Result result{};
			int frames = 0;
			while (!result.line && frames < 200) {
				result = gate.Next(kFrame, waits, silent);
				++frames;
			}
			Expect(result.line && result.why == Gate::Why::kUnheard, "used anyway when nothing has been heard");
			Expect(frames >= 79 && frames <= 81, "after the eight second wait");
		}

		// Even once a voice has been heard, a line whose voice isn't heard in time is
		// still used after the wait, never dropped. (A slow reply after one heard
		// voice used to switch the camera off for the rest of the session.)
		{
			Gate gate;
			std::set<std::uint32_t> talking{ 100 };
			const auto audible = [&](std::uint32_t s) { return talking.count(s) > 0; };
			gate.Push(1, 100, false, false);
			static_cast<void>(gate.Next(kFrame, waits, audible));
			Expect(gate.HeardEver(), "heard once");
			talking.clear();
			for (int round = 0; round < 3; ++round) {
				gate.Push(2 + round, 200, false, false);
				Gate::Result result{};
				int frames = 0;
				while (!result.line && frames < 200) {
					result = gate.Next(kFrame, waits, audible);
					Expect(result.dropped == 0, "nothing dropped");
					++frames;
				}
				Expect(result.line && result.why == Gate::Why::kUnheard, "used after the wait, every time");
			}
			Expect(gate.Empty(), "all used");
		}

		// A short wait, as the player may set it: still used, just sooner.
		{
			Gate gate;
			gate.Push(4, 100, false, false);
			Gate::Result result{};
			int frames = 0;
			while (!result.line && frames < 50) {
				result = gate.Next(kFrame, Gate::Waits{ 0.9f, 0.75f }, [](std::uint32_t) { return false; });
				++frames;
			}
			Expect(result.line && frames >= 9 && frames <= 10, "a 0.9s wait uses the line after 0.9s");
		}

		// The player's lines go after a short beat, or at once if their voice starts.
		{
			Gate gate;
			const auto silent = [](std::uint32_t) { return false; };
			gate.Push(5, 0x14, true, false);
			int frames = 0;
			Gate::Result result{};
			while (!result.line && frames < 50) {
				result = gate.Next(kFrame, waits, silent);
				++frames;
			}
			Expect(result.line && result.why == Gate::Why::kPlayerLine, "player line goes");
			Expect(frames >= 7 && frames <= 9, "after about three quarters of a second");
		}

		// Waiting switched off.
		{
			Gate gate;
			gate.Push(9, 100, false, false);
			const auto result = gate.Next(kFrame, Gate::Waits{ 0.0f, 0.75f }, [](std::uint32_t) { return false; });
			Expect(result.line && result.why == Gate::Why::kNoWait, "no wait means at once");
		}

		// The player's line isn't dropped by a short NPC wait.
		{
			Gate gate;
			gate.Push(3, 0x14, true, false);
			Gate::Result result{};
			for (int i = 0; i < 6 && !result.line; ++i) {
				result = gate.Next(kFrame, Gate::Waits{ 0.2f, 0.75f }, [](std::uint32_t) { return false; });
				Expect(result.dropped == 0, "a player line is never dropped");
			}
		}

		// Overflow drops the oldest.
		{
			Gate gate;
			for (int i = 0; i < 20; ++i) {
				gate.Push(i, 100, false, false);
			}
			Expect(gate.Size() == Gate::kCapacity, "capacity");
			const auto result = gate.Next(kFrame, waits, [](std::uint32_t) { return false; });
			Expect(result.dropped == 4, "overflow is reported");
		}
	}
}

int main()
{
	ParseTests();
	GateTests();
	std::cout << "SkyrimNet tests passed\n";
	return EXIT_SUCCESS;
}
