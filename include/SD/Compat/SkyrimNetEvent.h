#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace SD::Compat::SkyrimNetEvent
{
	// One SkyrimNet dialogue event, reduced to what the camera needs. Pure, so it
	// can be parsed on SkyrimNet's worker thread (no game objects are touched) and
	// tested without the game.
	//
	// SkyrimNet's public API (v5+) delivers events as JSON:
	//   {
	//     "type": "dialogue",
	//     "data": "{\"text\":\"...\",\"speaker\":\"Lydia\"}",   (or an object)
	//     "originatingActorFormId": 1234,
	//     "targetActorFormId": 5678,
	//     "id": 42
	//   }
	// The shape of `data` has varied between SkyrimNet versions, so the line's text
	// is looked for under several names and the form IDs are read from numbers or
	// strings.
	enum class Kind : std::uint8_t
	{
		kNpc,           // "dialogue": someone other than the player said a line
		kPlayerText,    // "dialogue_player_text": the player typed a line
		kPlayerSpeech,  // "dialogue_player_stt": the player spoke into the microphone
	};

	struct Line
	{
		Kind          kind{ Kind::kNpc };
		std::uint32_t speaker{ 0 };  // originatingActorFormId
		std::uint32_t target{ 0 };   // targetActorFormId; 0 when SkyrimNet named nobody
		std::string   text{};
		std::uint64_t id{ 0 };
	};

	// The player's reference. Player events that leave the speaker out are theirs.
	inline constexpr std::uint32_t kPlayerFormId = 0x14;

	// Longest line kept. The camera only needs the length; the text is for the log.
	inline constexpr std::size_t kMaxText = 2000;

	[[nodiscard]] inline bool IsPlayer(Kind a_kind) noexcept
	{
		return a_kind != Kind::kNpc;
	}

	[[nodiscard]] inline std::optional<Kind> KindOf(std::string_view a_type) noexcept
	{
		if (a_type == "dialogue") {
			return Kind::kNpc;
		}
		if (a_type == "dialogue_player_text") {
			return Kind::kPlayerText;
		}
		if (a_type == "dialogue_player_stt") {
			return Kind::kPlayerSpeech;
		}
		return std::nullopt;
	}

	// A form ID from a JSON number, or from a decimal or 0x-hex string. 0 when
	// there's nothing usable.
	[[nodiscard]] inline std::uint32_t FormIdOf(const nlohmann::json& a_value) noexcept
	{
		if (a_value.is_number_unsigned()) {
			const auto value = a_value.get<std::uint64_t>();
			return value <= 0xFFFFFFFFull ? static_cast<std::uint32_t>(value) : 0u;
		}
		if (a_value.is_number_integer()) {
			// Some serializers write a form ID as a signed 32-bit value, so FE-prefixed
			// (light plugin) IDs come out negative.
			const auto value = a_value.get<std::int64_t>();
			if (value >= 0 && value <= 0xFFFFFFFFll) {
				return static_cast<std::uint32_t>(value);
			}
			if (value < 0 && value >= -0x80000000ll) {
				return static_cast<std::uint32_t>(static_cast<std::int32_t>(value));
			}
			return 0u;
		}
		if (a_value.is_string()) {
			const auto& text = a_value.get_ref<const std::string&>();
			if (text.empty() || text.size() > 18) {
				return 0u;
			}
			const bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
			char*      end = nullptr;
			const auto value = std::strtoull(text.c_str(), &end, hex ? 16 : 10);
			if (!end || *end != '\0' || value > 0xFFFFFFFFull) {
				return 0u;
			}
			return static_cast<std::uint32_t>(value);
		}
		return 0u;
	}

	namespace detail
	{
		[[nodiscard]] inline std::uint32_t FirstFormId(const nlohmann::json& a_object,
			std::initializer_list<const char*> a_keys) noexcept
		{
			for (const char* key : a_keys) {
				const auto it = a_object.find(key);
				if (it != a_object.end()) {
					if (const auto id = FormIdOf(*it); id != 0) {
						return id;
					}
				}
			}
			return 0u;
		}

		// The spoken words in an event's data object.
		[[nodiscard]] inline std::string TextIn(const nlohmann::json& a_object)
		{
			for (const char* key : { "text", "dialogue", "line", "content", "message", "speech" }) {
				const auto it = a_object.find(key);
				if (it != a_object.end() && it->is_string()) {
					return it->get<std::string>();
				}
			}
			return {};
		}

		[[nodiscard]] inline std::string Trimmed(std::string a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\r\n");
			if (first == std::string::npos) {
				return {};
			}
			const auto last = a_text.find_last_not_of(" \t\r\n");
			a_text = a_text.substr(first, last - first + 1);
			if (a_text.size() > kMaxText) {
				a_text.resize(kMaxText);
			}
			return a_text;
		}
	}

	// Parse one event. a_type is the event type the callback was registered for,
	// which decides the kind even if the JSON's own "type" disagrees. Returns
	// nothing for malformed JSON or an NPC line with no speaker. Never throws.
	[[nodiscard]] inline std::optional<Line> Parse(std::string_view a_type, std::string_view a_json) noexcept
	{
		try {
			const auto kind = KindOf(a_type);
			if (!kind) {
				return std::nullopt;
			}

			const auto event = nlohmann::json::parse(a_json.begin(), a_json.end(), nullptr, false);
			if (!event.is_object()) {
				return std::nullopt;
			}

			Line line{};
			line.kind = *kind;
			line.speaker = detail::FirstFormId(event, { "originatingActorFormId", "originatorFormId", "speakerFormId" });
			line.target = detail::FirstFormId(event, { "targetActorFormId", "targetFormId", "listenerFormId" });

			if (const auto it = event.find("id"); it != event.end() && it->is_number_unsigned()) {
				line.id = it->get<std::uint64_t>();
			}

			// `data` is a JSON string holding an object, an object, or plain text.
			if (const auto data = event.find("data"); data != event.end()) {
				if (data->is_object()) {
					line.text = detail::TextIn(*data);
					if (line.speaker == 0) {
						line.speaker = detail::FirstFormId(*data, { "speakerFormId", "originatingActorFormId" });
					}
					if (line.target == 0) {
						line.target = detail::FirstFormId(*data, { "targetFormId", "targetActorFormId", "listenerFormId" });
					}
				} else if (data->is_string()) {
					const auto& raw = data->get_ref<const std::string&>();
					const auto  inner = nlohmann::json::parse(raw, nullptr, false);
					if (inner.is_object()) {
						line.text = detail::TextIn(inner);
						if (line.speaker == 0) {
							line.speaker = detail::FirstFormId(inner, { "speakerFormId", "originatingActorFormId" });
						}
						if (line.target == 0) {
							line.target = detail::FirstFormId(inner, { "targetFormId", "targetActorFormId", "listenerFormId" });
						}
					} else if (inner.is_string()) {
						line.text = inner.get<std::string>();
					} else {
						line.text = raw;
					}
				}
			}
			if (line.text.empty()) {
				line.text = detail::TextIn(event);
			}
			line.text = detail::Trimmed(std::move(line.text));

			if (IsPlayer(line.kind)) {
				// The player's own line: the speaker is the player whatever the event says,
				// and a target equal to the player means nobody in particular.
				line.speaker = kPlayerFormId;
				if (line.target == kPlayerFormId) {
					line.target = 0;
				}
			} else {
				if (line.speaker == 0 || line.speaker == kPlayerFormId) {
					// An NPC event with no NPC behind it can't be filmed.
					return std::nullopt;
				}
				if (line.target == line.speaker) {
					line.target = 0;
				}
			}
			return line;
		} catch (...) {
			return std::nullopt;
		}
	}
}
