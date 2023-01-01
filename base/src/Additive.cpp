#include "Additive.h"

#include "JuceHeader.h"

// This was a helpful page: https://meettechniek.info/additional/additive-synthesis.html

namespace midikraft {

	std::vector<float> Additive::createSamplesFromHarmonics(Harmonics const &harmonics)
	{
		// This is just a sinus series, so theoretically we could run a loop and sum up sine functions...
		// But this is what the inverse FFT is for, isn't it?

		// Create the FFT object with 256 bins (2^8)
		dsp::FFT fft(8);

		// Build the input array
		std::vector<dsp::Complex<float>> input(256);
		for (auto harmonic : harmonics.harmonics()) {
            if (harmonic.first > 0) {
                input[static_cast<size_t>(harmonic.first)] = dsp::Complex<float>(0.0f, harmonic.second);
            }
            else {
                jassertfalse;
            }
		}

		// Reserve output array and run inverse transform
		std::vector<dsp::Complex<float>> output(256);
		fft.perform(&input[0], &output[0], true);

		// Extract usable part of wave...
		std::vector<float> result;
		for (auto o : output) {
			result.push_back(o.real());
		}
		return result;

	}

	void Additive::Harmonics::setHarmonic(int harmonicNumber, float value)
	{
		if (value > 0.0f) {
			harmonics_[harmonicNumber] = value;
		}
		else {
			auto found = harmonics_.find(harmonicNumber);
			if (found != harmonics_.end()) harmonics_.erase(found);
		}
	}

}
