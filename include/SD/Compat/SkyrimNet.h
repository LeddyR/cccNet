#pragma once

#include "SD/Compat/SkyrimNetEvent.h"

namespace SD::Compat
{
	// SkyrimNet's public C++ API, resolved at runtime from SkyrimNet.dll. Nothing
	// is linked statically and nothing of SkyrimNet's is modified: this only
	// listens to its dialogue events.
	//
	// Needs API v5 or newer (event callbacks). Subscribes to "dialogue",
	// "dialogue_player_text" and "dialogue_player_stt".
	//
	// SkyrimNet calls back on its own worker threads. The callbacks only parse the
	// JSON and queue plain data under a mutex; they never touch a game object. The
	// Director drains the queue on the main thread each frame.
	//
	// ABI: SkyrimNet's API passes std::function across the DLL boundary. The
	// callbacks handed over are plain function pointers, which sit in
	// std::function's small buffer, so no heap allocation ever crosses the
	// boundary and the CRT link mode doesn't matter.
	class SkyrimNet
	{
	public:
		// Once, at kDataLoaded, after every SKSE plugin has loaded. Safe without
		// SkyrimNet; logs what it found.
		static void Connect();

		// SkyrimNet.dll is loaded.
		[[nodiscard]] static bool Installed() noexcept;

		// The dialogue callbacks are registered.
		[[nodiscard]] static bool Listening() noexcept;

		// SkyrimNet's API version, or 0.
		[[nodiscard]] static int ApiVersion() noexcept;

		// Whether callbacks queue what they receive. Off while the cinematic mode is
		// off, so nothing piles up.
		static void SetAccepting(bool a_accepting) noexcept;

		// Everything queued since the last call, oldest first. Main thread.
		[[nodiscard]] static std::vector<SkyrimNetEvent::Line> Drain();

		// Drop anything queued.
		static void Clear();
	};
}
