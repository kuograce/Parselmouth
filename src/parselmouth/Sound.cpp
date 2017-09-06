/*
 * Copyright (C) 2017  Yannick Jadoul
 *
 * This file is part of Parselmouth.
 *
 * Parselmouth is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Parselmouth is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Parselmouth.  If not, see <http://www.gnu.org/licenses/>
 */

#include "Parselmouth.h"
#include "TimeClassAspects.h"

#include "praat/MelderUtils.h"
#include "utils/SignatureCast.h"
#include "utils/pybind11/ImplicitStringToEnumConversion.h"
#include "utils/pybind11/NumericPredicates.h"
#include "utils/pybind11/Optional.h"

#include "dwtools/Sound_extensions.h"
#include "dwtools/Sound_to_MFCC.h"
#include "fon/Sound_and_Spectrogram.h"
#include "fon/Sound_and_Spectrum.h"
#include "fon/Sound_to_Formant.h"
#include "fon/Sound_to_Harmonicity.h"
#include "fon/Sound_to_Intensity.h"
#include "fon/Sound_to_Pitch.h"

#include <pybind11/numpy.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace py::literals;

namespace parselmouth {

namespace {

template <typename T, typename Container>
OrderedOf<T> referencesToOrderedOf(const Container &container) // TODO type_caster?
{
	OrderedOf<T> orderedOf;
	std::for_each(begin(container), end(container), [&orderedOf] (T &item) { orderedOf.addItem_ref(&item); });
	return orderedOf;
}

} // namespace

enum class SoundFileFormat // TODO Nest within Sound?
{
	WAV,
	AIFF,
	AIFC,
	NEXT_SUN,
	NIST,
	FLAC,
	KAY,
	SESAM,
	WAV_24,
	WAV_32,
	RAW_8_SIGNED,
	RAW_8_UNSIGNED,
	RAW_16_BE,
	RAW_16_LE,
	RAW_24_BE,
	RAW_24_LE,
	RAW_32_BE,
	RAW_32_LE
};

// TODO Export befóre using default values for them
// TODO Can be nested within Sound? Valid documentation (i.e. parselmouth.Sound.WindowShape instead of parselmouth.WindowShape)?

void Binding<WindowShape>::init() {
	value("RECTANGULAR", kSound_windowShape_RECTANGULAR);
	value("TRIANGULAR", kSound_windowShape_TRIANGULAR);
	value("PARABOLIC", kSound_windowShape_PARABOLIC);
	value("HANNING", kSound_windowShape_HANNING);
	value("HAMMING", kSound_windowShape_HAMMING);
	value("GAUSSIAN1", kSound_windowShape_GAUSSIAN_1);
	value("GAUSSIAN2", kSound_windowShape_GAUSSIAN_2);
	value("GAUSSIAN3", kSound_windowShape_GAUSSIAN_3);
	value("GAUSSIAN4", kSound_windowShape_GAUSSIAN_4);
	value("GAUSSIAN5", kSound_windowShape_GAUSSIAN_5);
	value("KAISER1", kSound_windowShape_KAISER_1);
	value("KAISER2", kSound_windowShape_KAISER_2);

	make_implicitly_convertible_from_string(*this, true);
}

void Binding<AmplitudeScaling>::init() {
	value("INTEGRAL", kSounds_convolve_scaling_INTEGRAL);
	value("SUM", kSounds_convolve_scaling_SUM);
	value("NORMALIZE", kSounds_convolve_scaling_NORMALIZE);
	value("PEAK_0_99", kSounds_convolve_scaling_PEAK_099);

	make_implicitly_convertible_from_string(*this, true);
}

void Binding<SignalOutsideTimeDomain>::init() {
	value("ZERO", kSounds_convolve_signalOutsideTimeDomain_ZERO);
	value("SIMILAR", kSounds_convolve_signalOutsideTimeDomain_SIMILAR);

	make_implicitly_convertible_from_string(*this, true);
}

void Binding<SoundFileFormat>::init() {
	value("WAV", SoundFileFormat::WAV);
	value("AIFF", SoundFileFormat::AIFF);
	value("AIFC", SoundFileFormat::AIFC);
	value("NEXT_SUN", SoundFileFormat::NEXT_SUN);
	value("NIST", SoundFileFormat::NIST);
	value("FLAC", SoundFileFormat::FLAC);
	value("KAY", SoundFileFormat::KAY);
	value("SESAM", SoundFileFormat::SESAM);
	value("WAV_24", SoundFileFormat::WAV_24);
	value("WAV_32", SoundFileFormat::WAV_32);
	value("RAW_8_SIGNED", SoundFileFormat::RAW_8_SIGNED);
	value("RAW_8_UNSIGNED", SoundFileFormat::RAW_8_UNSIGNED);
	value("RAW_16_BE", SoundFileFormat::RAW_16_BE);
	value("RAW_16_LE", SoundFileFormat::RAW_16_LE);
	value("RAW_24_BE", SoundFileFormat::RAW_24_BE);
	value("RAW_24_LE", SoundFileFormat::RAW_24_LE);
	value("RAW_32_BE", SoundFileFormat::RAW_32_BE);
	value("RAW_32_LE", SoundFileFormat::RAW_32_LE);

	make_implicitly_convertible_from_string(*this, true);
}

void Binding<SpectralAnalysisWindowShape>::init() {
	value("SQUARE", kSound_to_Spectrogram_windowShape_SQUARE);
	value("HAMMING", kSound_to_Spectrogram_windowShape_HAMMING);
	value("BARTLETT", kSound_to_Spectrogram_windowShape_BARTLETT);
	value("WELCH", kSound_to_Spectrogram_windowShape_WELCH);
	value("HANNING", kSound_to_Spectrogram_windowShape_HANNING);
	value("GAUSSIAN", kSound_to_Spectrogram_windowShape_GAUSSIAN);

	make_implicitly_convertible_from_string(*this, true);
}

void Binding<Sound>::init() {
	using signature_cast_placeholder::_;

	initTimeFrameSampled(*this);

	def(py::init([] (py::array_t<double, 0> values, Positive<double> samplingFrequency, double startTime) { // TODO py::array::f_style to be able to memcpy / NUMmatrix_copyElements ?
		    auto ndim = values.ndim();
		    if (ndim > 2) {
			    throw py::value_error("Cannot create Sound from an array with more than 2 dimensions");
		    }

		    auto nx = values.shape(0);
		    auto ny = ndim == 2 ? values.shape(1) : 1;
		    auto result = Sound_create(ny, startTime, startTime + nx / samplingFrequency, nx, 1.0 / samplingFrequency, startTime + 0.5 / samplingFrequency);

		    if (ndim == 2) {
			    auto unchecked = values.unchecked<2>();
			    for (ssize_t i = 0; i < nx; ++i)
				    for (ssize_t j = 0; j < ny; ++j)
					    result->z[j+1][i+1] = unchecked(i, j);
		    }
		    else {
			    auto unchecked = values.unchecked<1>();
			    for (ssize_t i = 0; i < nx; ++i)
				    result->z[1][i+1] = unchecked(i);
		    }

		    return result;
	    }),
	    "values"_a, "sampling_frequency"_a = 44100.0, "start_time"_a = 0.0);

	def(py::init([] (const std::u32string &filePath) {
		    auto file = pathToMelderFile(filePath);
		    return Sound_readFromSoundFile(&file);
	    }),
	    "file_path"_a);

	// TODO Constructor from few special file formats that are not detectable by header
	// TODO Constructor from file or io.IOBase?
	// TODO Constructor from Praat-format file?
	// TODO Constructor from py::buffer?
	// TODO Empty constructor?

	def("save",
	    [] (Sound self, const std::u32string &filePath, SoundFileFormat format) {
		    auto file = pathToMelderFile(filePath);
		    switch(format) {
		    case SoundFileFormat::WAV:
			    Sound_saveAsAudioFile(self, &file, Melder_WAV, 16);
			    break;

		    case SoundFileFormat::AIFF:
			    Sound_saveAsAudioFile(self, &file, Melder_AIFF, 16);
			    break;

		    case SoundFileFormat::AIFC:
			    Sound_saveAsAudioFile(self, &file, Melder_AIFC, 16);
			    break;

		    case SoundFileFormat::NEXT_SUN:
			    Sound_saveAsAudioFile(self, &file, Melder_NEXT_SUN, 16);
			    break;

		    case SoundFileFormat::NIST:
			    Sound_saveAsAudioFile(self, &file, Melder_NIST, 16);
			    break;

		    case SoundFileFormat::FLAC:
			    Sound_saveAsAudioFile(self, &file, Melder_FLAC, 16);
			    break;

		    case SoundFileFormat::KAY:
			    Sound_saveAsKayFile (self, &file);
			    break;

		    case SoundFileFormat::SESAM:
			    Sound_saveAsSesamFile (self, &file);
			    break;

		    case SoundFileFormat::WAV_24:
			    Sound_saveAsAudioFile(self, &file, Melder_WAV, 24);
			    break;

		    case SoundFileFormat::WAV_32:
			    Sound_saveAsAudioFile(self, &file, Melder_WAV, 32);
			    break;

		    case SoundFileFormat::RAW_8_SIGNED:
			    Sound_saveAsRawSoundFile(self, &file, Melder_LINEAR_8_SIGNED);
			    break;

		    case SoundFileFormat::RAW_8_UNSIGNED:
			    Sound_saveAsRawSoundFile(self, &file, Melder_LINEAR_8_UNSIGNED);
			    break;

		    case SoundFileFormat::RAW_16_BE:
			    Sound_saveAsRawSoundFile(self, &file, Melder_LINEAR_16_BIG_ENDIAN);
			    break;

		    case SoundFileFormat::RAW_16_LE:
			    Sound_saveAsRawSoundFile(self, &file, Melder_LINEAR_16_LITTLE_ENDIAN);
			    break;

		    case SoundFileFormat::RAW_24_BE:
			    Sound_saveAsRawSoundFile(self, &file, Melder_LINEAR_24_BIG_ENDIAN);
			    break;

		    case SoundFileFormat::RAW_24_LE:
			    Sound_saveAsRawSoundFile(self, &file, Melder_LINEAR_24_LITTLE_ENDIAN);
			    break;

		    case SoundFileFormat::RAW_32_BE:
			    Sound_saveAsRawSoundFile(self, &file, Melder_LINEAR_32_BIG_ENDIAN);
			    break;

		    case SoundFileFormat::RAW_32_LE:
			    Sound_saveAsRawSoundFile(self, &file, Melder_LINEAR_32_LITTLE_ENDIAN);
			    break;
		    }
	    },
	    "file_path"_a, "format"_a);
	// TODO Determine file format based on extension, and make format optional
	// TODO Coordinate this save function with the (future) save in Data

	def("get_number_of_channels", [](Sound self) { return self->ny; });

	def_readonly("n_channels", &structSound::ny); // TODO Remove static_cast once SampledXY is exported, or once this is fixed

	def("get_number_of_samples", [](Sound self) { return self->nx; });

	def_readonly("n_samples", &structSound::nx); // TODO Remove static_cast once Sampled is exported, or once this is fixed

	def("get_sampling_period", [](Sound self) { return self->dx; });

	def_property("sampling_period",
	             [](Sound self) { return self->dx; },
	             [](Sound self, double period) { Sound_overrideSamplingFrequency(self, 1 / period); });

	def("get_sampling_frequency", [](Sound self) { return 1 / self->dx; });

	def_property("sampling_frequency",
	             [](Sound self) { return 1 / self->dx; },
	             [](Sound self, double frequency) { Sound_overrideSamplingFrequency(self, frequency); });

	def("get_time_from_index", // TODO PraatIndex to distinguish 1-based silliness? // TODO Get time from sample number...
	    [](Sound self, long sample) { return Sampled_indexToX(self, sample); },
	    "sample"_a);

	def("get_index_from_time",
	    signature_cast<_ (Sound, _)>(Sampled_xToIndex),
	    "time"_a);

	// TODO Get value at time & sample number

	// TODO Minimum & maximum (Vector?)

	def("get_nearest_zero_crossing", // TODO Channel is CHANNEL
	    [](Sound self, double time, long channel) {
		    if (channel > self->ny) channel = 1;
		    return Sound_getNearestZeroCrossing (self, time, channel);
	    },
	    "time"_a, "channel"_a = 1);

	// TODO Get mean (Vector?)

	def("get_root_mean_square",
	    [](Sound self, optional<double> fromTime, optional<double> toTime) { return Sound_getRootMeanSquare(self, fromTime.value_or(self->xmin), toTime.value_or(self->xmax)); },
	    "from_time"_a = nullopt, "to_time"_a = nullopt);

	// TODO Get standard deviation

	def("get_rms",
	    [](Sound self, optional<double> fromTime, optional<double> toTime) { return Sound_getRootMeanSquare(self, fromTime.value_or(self->xmin), toTime.value_or(self->xmax)); },
	    "from_time"_a = nullopt, "to_time"_a = nullopt);

	def("get_energy",
	    [] (Sound self, optional<double> fromTime, optional<double> toTime) { return Sound_getEnergy(self, fromTime.value_or(self->xmin), toTime.value_or(self->xmax)); },
	    "from_time"_a = nullopt, "to_time"_a = nullopt);

	def("get_power",
	    [](Sound self, optional<double> fromTime, optional<double> toTime) { return Sound_getPower(self, fromTime.value_or(self->xmin), toTime.value_or(self->xmax)); },
	    "from_time"_a = nullopt, "to_time"_a = nullopt);

	def("get_energy_in_air",
	    &Sound_getEnergyInAir);

	def("get_power_in_air",
	    &Sound_getPowerInAir);

	def("get_intensity", // TODO Get intensity (dB) -> get_intensity_dB/get_intensity_db ?
	    &Sound_getIntensity_dB);

	def("reverse",
	    [](Sound self, optional<double> fromTime, optional<double> toTime) { Sound_reverse(self, fromTime.value_or(self->xmin), toTime.value_or(self->xmax)); },
	    "from_time"_a = nullopt, "to_time"_a = nullopt);

	// TODO Formula and Formula (part) (reimplement from Vector/Matrix because of different parameters, i.e. channels?)

	def("multiply_by_window",
	    &Sound_multiplyByWindow,
	    "window_shape"_a);

	def("scale_intensity",
	    &Sound_scaleIntensity,
	    "new_average_intensity"_a);

	// TODO Set value at sample number

	def("set_to_zero", // TODO Set part to zero ?
	    [](Sound self, optional<double> fromTime, optional<double> toTime, bool roundToNearestZeroCrossing) { Sound_setZero(self, fromTime.value_or(self->xmin), toTime.value_or(self->xmax), roundToNearestZeroCrossing); },
	    "from_time"_a = nullopt, "to_time"_a = nullopt, "round_to_nearest_zero_crossing"_a = true);

	def("override_sampling_frequency",
	    signature_cast<_ (_, Positive<double>)>(Sound_overrideSamplingFrequency),
	    "new_frequency"_a);

	// TODO Filter with one formant (in-line)...

	def("pre_emphasize",
	    [](Sound self, double fromFrequency, bool normalize) {
		    Sound_preEmphasis(self, fromFrequency);
		    if (normalize) {
			    Vector_scale(self, 0.99);
		    }
	    },
	    "from_frequency"_a = 50.0, "normalize"_a = true); // TODO Not POSITIVE now!?

	def("de_emphasize",
	    [] (Sound self, double fromFrequency, bool normalize) {
		    Sound_deEmphasis (self, fromFrequency);
		    if (normalize) {
			    Vector_scale(self, 0.99);
		    }
	    },
	    "from_frequency"_a = 50.0, "normalize"_a = true); // TODO Not POSITIVE now!?

	def("convert_to_mono",
	    &Sound_convertToMono);

	def("convert_to_stereo",
	    &Sound_convertToStereo);

	def("extract_all_channels",
	    [] (Sound self) {
		    std::vector<autoSound> result;
		    result.reserve(self->ny);
		    for (auto i = 1; i <= self->ny; ++i) {
			    result.emplace_back(Sound_extractChannel(self, i));
		    }
		    return result;
	    });

	def("extract_channel", // TODO Channel POSITIVE? (Actually CHANNEL; >= 1, but does not always have intended result (e.g., Set value at sample...))
	    &Sound_extractChannel,
	    "channel"_a);

	def("extract_channel", // TODO Channel enum type?
	    [] (Sound self, std::string channel) {
		    std::transform(channel.begin(), channel.end(), channel.begin(), tolower);
		    if (channel == "left")
			    return Sound_extractChannel(self, 1);
		    if (channel == "right")
			    return Sound_extractChannel(self, 2);
		    Melder_throw(U"'channel' can only be 'left' or 'right'"); // TODO Melder_throw or throw PraatError ?
	    });

	def("extract_left_channel",
	    [] (Sound self) { return Sound_extractChannel(self, 1); });

	def("extract_right_channel",
	    [] (Sound self) { return Sound_extractChannel(self, 2); });

	def("extract_part", // TODO Something for optional<double> for from and to in Sounds?
	    [] (Sound self, optional<double> fromTime, optional<double> toTime, kSound_windowShape windowShape, Positive<double> relativeWidth, bool preserveTimes) { return Sound_extractPart(self, fromTime.value_or(self->xmin), toTime.value_or(self->xmax), windowShape, relativeWidth, preserveTimes); },
	    "from_time"_a = nullopt, "to_time"_a = nullopt, "window_shape"_a = kSound_windowShape_RECTANGULAR, "relative_width"_a = 1.0, "preserve_times"_a = false);

	def("extract_part_for_overlap",
	    [] (Sound self, optional<double> fromTime, optional<double> toTime, Positive<double> overlap) { return Sound_extractPartForOverlap(self, fromTime.value_or(self->xmin), toTime.value_or(self->xmax), overlap); },
	    "from_time"_a = nullopt, "to_time"_a = nullopt, "overlap"_a);

	def("resample",
	    &Sound_resample,
	    "new_frequency"_a, "precision"_a = 50);

	def("lengthen", // TODO Lengthen (Overlap-add) ?
	    [](Sound self, Positive<double> minimumPitch, Positive<double> maximumPitch, Positive<double> factor) {
		    if (minimumPitch >= maximumPitch)
			    Melder_throw (U"Maximum pitch should be greater than minimum pitch.");
		    return Sound_lengthen_overlapAdd(self, minimumPitch, maximumPitch, factor);
	    },
	    "minimum_pitch"_a = 75.0, "maximum_pitch"_a = 600.0, "factor"_a);

	def("deepen_band_modulation",
	    signature_cast<_ (_, Positive<double>, Positive<double>, Positive<double>, Positive<double>, Positive<double>, Positive<double>)>(Sound_deepenBandModulation),
	    "enhancement"_a = 20.0, "from_frequency"_a = 300.0, "to_frequency"_a = 8000.0, "slow_modulation"_a = 3.0, "fast_modulation"_a = 30.0, "band_smoothing"_a = 100.0);

	def("to_pitch",
	    [](Sound self, optional<Positive<double>> timeStep, Positive<double> pitchFloor, Positive<double> pitchCeiling) { return Sound_to_Pitch(self, timeStep ? static_cast<double>(*timeStep) : 0.0, pitchFloor, pitchCeiling); },
	    "time_step"_a = nullopt, "pitch_floor"_a = 75.0, "pitch_ceiling"_a = 600.0);
	// TODO To Pitch...

	def("to_harmonicity_ac",
	    signature_cast<_ (_, Positive<double>, Positive<double>, _, Positive<double>)>(Sound_to_Harmonicity_ac),
	    "time_step"_a = 0.01, "minimum_pitch"_a = 75.0, "silence_treshold"_a = 0.1, "periods_per_window"_a = 1.0);

	def("to_harmonicity_cc",
	    signature_cast<_ (_, Positive<double>, Positive<double>, _, Positive<double>)>(Sound_to_Harmonicity_cc),
	    "time_step"_a = 0.01, "minimum_pitch"_a = 75.0, "silence_treshold"_a = 0.1, "periods_per_window"_a = 1.0);

	def("to_harmonicity_gne",
	    signature_cast<_ (_, Positive<double>, Positive<double>, Positive<double>, Positive<double>)>(Sound_to_Harmonicity_GNE),
	    "minimum_frequency"_a = 500.0, "maximum_frequency"_a = 4500.0, "bandwidth"_a = 1000.0, "step"_a = 80.0);

	// TODO to_harmonicity(SoundToHarmonicityMethod) ?

	def("autocorrelate",
	    &Sound_autoCorrelate,
	    "scaling"_a = kSounds_convolve_scaling_PEAK_099, "signal_outside_time_domain"_a = kSounds_convolve_signalOutsideTimeDomain_ZERO);

	def("to_spectrum",
	    signature_cast<_ (_, bool)>(Sound_to_Spectrum),
	    "fast"_a = true);

	def("to_spectrogram",
	    [](Sound self, Positive<double> windowLength, Positive<double> maximumFrequency, Positive<double> timeStep, Positive<double> frequencyStep, kSound_to_Spectrogram_windowShape windowShape) { return Sound_to_Spectrogram(self, windowLength, maximumFrequency, timeStep, frequencyStep, windowShape, 8.0, 8.0); },
	    "window_length"_a = 0.005, "maximum_frequency"_a = 5000.0, "time_step"_a = 0.002, "frequency_step"_a = 20.0, "window_shape"_a = kSound_to_Spectrogram_windowShape_GAUSSIAN);

	def("to_formant_burg", // TODO Praat has Max. number of formants as REAL? What the hell? "Pi formants for me, please."? (I know, I know; see Praat documentation)
	    [](Sound self, optional<Positive<double>> timeStep, Positive<double> maxNumberOfFormants, double maximumFormant, Positive<double> windowLength, Positive<double> preEmphasisFrom) { return Sound_to_Formant_burg(self, timeStep ? static_cast<double>(*timeStep) : 0.0, maxNumberOfFormants, maximumFormant, windowLength, preEmphasisFrom); },
		"time_step"_a = nullopt, "max_number_of_formants"_a = 5.0, "maximum_formant"_a = 5500.0, "window_length"_a = 0.025, "pre_emphasis_from"_a = 50.0);
	// TODO To Formant...

	def("to_intensity",
	    [](Sound self, Positive<double> minimumPitch, optional<Positive<double>> timeStep, bool subtractMean) { return Sound_to_Intensity(self, minimumPitch, timeStep ? static_cast<double>(*timeStep) : 0.0, subtractMean); },
	    "minimum_pitch"_a = 100.0, "time_step"_a = nullopt, "subtract_mean"_a = true);

	// TODO Filters
	// TODO Group different filters into enum/class/...?

	def_static("combine_to_stereo",
	           [] (const std::vector<std::reference_wrapper<structSound>> &sounds) {
		           auto ordered = referencesToOrderedOf<structSound>(sounds);
		           return Sounds_combineToStereo(&ordered);
	           },
	           "sounds"_a);

	def_static("concatenate",
	           [] (const std::vector<std::reference_wrapper<structSound>> &sounds, NonNegative<double> overlap) {
		           auto ordered = referencesToOrderedOf<structSound>(sounds);
		           return Sounds_concatenate(ordered, overlap);
	           },
	           "sounds"_a, "overlap"_a = 0.0);
	// TODO concatenate recoverably (dependends on having TextGrid)
	// TODO concatenate as member function?

	def("convolve",
	    &Sounds_convolve,
	    "other"_a.none(false), "scaling"_a = kSounds_convolve_scaling_PEAK_099, "signal_outside_time_domain"_a = kSounds_convolve_signalOutsideTimeDomain_ZERO);

	def("cross_correlate",
	    &Sounds_crossCorrelate,
	    "other"_a.none(false), "scaling"_a = kSounds_convolve_scaling_PEAK_099, "signal_outside_time_domain"_a = kSounds_convolve_signalOutsideTimeDomain_ZERO);
	// TODO Cross-correlate (short)?

	// TODO For some reason praat_David_init.cpp also still contains Sound functionality
}

} // namespace parselmouth
