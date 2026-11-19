#pragma once

namespace SD::Scene
{
	// Heads turn to whoever is talking in a SkyrimNet conversation.
	//
	// SkyrimNet's lines don't go through the game's dialogue system, so nobody
	// gets the head tracking a normal conversation gives them: the person spoken
	// to keeps looking wherever they were until it's their turn, and everyone
	// else at the table looks away. While a SkyrimNet line is being said:
	//
	// - the person it's said to, and every NPC within range of the speaker, look
	//   at the speaker;
	// - the speaker looks at the person they're talking to.
	//
	// It uses the engine's dialogue head-tracking slot, as the player's own
	// conversations do (see Presence), refreshed every frame and handed back
	// afterwards: a slot still pointing where this set it is cleared, one another
	// mod has changed since is left alone. Heads and eyes only; nobody's body or
	// AI is moved, and the player's own head is never touched.
	//
	// Main thread only.
	class SkyrimNetGaze
	{
	public:
		// A SkyrimNet line has started. a_listener may be null. Attention holds for
		// a_seconds, or until the next line.
		static void Speak(RE::Actor* a_speaker, RE::Actor* a_listener, float a_seconds, float a_range);

		// Every frame while the SkyrimNet mode is on.
		static void Update(float a_delta);

		// Hand every head back. Safe to call any time.
		static void Release();
	};
}
