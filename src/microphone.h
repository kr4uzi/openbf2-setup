#pragma once

#include "task.h"

#include <functional>
#include <memory>

namespace setup {
	enum class MicrophoneAccess { undetermined, granted, denied };

	class Microphone
	{
		struct Impl;
		std::unique_ptr<Impl> m_Impl;

	public:
		// tests never touch the microphone or its permission prompt
		explicit Microphone(bool allowed);
		~Microphone();

		bool Allowed() const;
		MicrophoneAccess Access() const;
		// the system asks the user, once. true if access was granted
		task<bool> Request();
		void OpenSystemSettings();

		// calls `level` with the loudest sample (0 to 1) of every buffer, on the thread of the window.
		// false if there is no input device
		bool Start(std::function<void(double)> level);
		void Stop();
	};
}
