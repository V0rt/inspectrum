/*
 *  Copyright (C) 2015, Mike Walters <mike@flomp.net>
 *  Copyright (C) 2015, Jared Boone <jared@sharebrained.com>
 *
 *  This file is part of inspectrum.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "inputsource.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <stdexcept>
#include <algorithm>

#include <QFileInfo>

#include <QElapsedTimer>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmapCache>
#include <QRect>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>


class ComplexF64SampleAdapter : public SampleAdapter {
public:
	size_t sampleSize() override {
		return sizeof(std::complex<double>);
	}
	
	void copyRange(const void* const src, size_t start, size_t length, std::complex<float>* const dest) override {
		auto s = reinterpret_cast<const std::complex<double>*>(src);
		std::transform(&s[start], &s[start + length], dest,
					   [](const std::complex<double>& v) -> std::complex<float> {
						   return { static_cast<float>(v.real()) , static_cast<float>(v.imag()) };
					   }
		);
	}
};

class ComplexF32SampleAdapter : public SampleAdapter {
public:
    size_t sampleSize() override {
        return sizeof(std::complex<float>);
    }

    void copyRange(const void* const src, size_t start, size_t length, std::complex<float>* const dest) override {
        auto s = reinterpret_cast<const std::complex<float>*>(src);
        std::copy(&s[start], &s[start + length], dest);
    }
};

class ComplexS32SampleAdapter : public SampleAdapter {
public:
	size_t sampleSize() override {
		return sizeof(std::complex<int32_t>);
	}
	
	void copyRange(const void* const src, size_t start, size_t length, std::complex<float>* const dest) override {
		auto s = reinterpret_cast<const std::complex<int32_t>*>(src);
		std::transform(&s[start], &s[start + length], dest,
					   [](const std::complex<int32_t>& v) -> std::complex<float> {
						   const float k = 1.0f / 2147483648.0f;
						   return { v.real() * k, v.imag() * k };
					   }
		);
	}
};

class ComplexS24SampleAdapter : public SampleAdapter {
public:
	size_t sampleSize() override {
		return 6; // 6 bytes per complex sample (3 bytes real + 3 bytes imag)
	}
	
	void copyRange(const void* const src, size_t start, size_t length, std::complex<float>* const dest) override {
		auto s = reinterpret_cast<const char*>(src);
		char* pSrc = (char*)s + start * 6;
		auto* pDst = (std::complex<float>*)dest;
		
		for (size_t i = 0; i < length; i++) {
			// Read real part (first 3 bytes)
			uint32_t real_value = *((uint32_t*)pSrc);
			pSrc += 3;
			int32_t real = real_value & 0x00FFFFFF;
			if (real & 0x00800000) { // Check highest bit
				real |= 0xFF000000;  // Extend sign
			}
			
			// Read imaginary part (next 3 bytes)
			uint32_t imag_value = *((uint32_t*)pSrc);
			pSrc += 3;
			int32_t imag = imag_value & 0x00FFFFFF;
			if (imag & 0x00800000) { // Check highest bit
				imag |= 0xFF000000;  // Extend sign
			}
			
			*pDst++ = std::complex<float>(real / 8388608.0f, imag / 8388608.0f);
		}
	}
};

class ComplexU24SampleAdapter : public SampleAdapter {
public:
	size_t sampleSize() override {
		return 6; // 6 bytes per complex sample (3 bytes real + 3 bytes imag)
	}
	
	void copyRange(const void* const src, size_t start, size_t length, std::complex<float>* const dest) override {
		auto s = reinterpret_cast<const char*>(src);
		char* pSrc = (char*)s + start * 6;
		auto* pDst = (std::complex<float>*)dest;
		
		for (size_t i = 0; i < length; i++) {
			// Read real part (first 3 bytes)
			uint32_t real_value = *((uint32_t*)pSrc);
			pSrc += 3;
			uint32_t real = real_value & 0x00FFFFFF;
			
			// Read imaginary part (next 3 bytes)
			uint32_t imag_value = *((uint32_t*)pSrc);
			pSrc += 3;
			uint32_t imag = imag_value & 0x00FFFFFF;
			
			*pDst++ = std::complex<float>(real / 16777216.0f, imag / 16777216.0f);
		}
	}
};

class ComplexS16SampleAdapter : public SampleAdapter {
public:
    size_t sampleSize() override {
        return sizeof(std::complex<int16_t>);
    }

    void copyRange(const void* const src, size_t start, size_t length, std::complex<float>* const dest) override {
        auto s = reinterpret_cast<const std::complex<int16_t>*>(src);
        std::transform(&s[start], &s[start + length], dest,
            [](const std::complex<int16_t>& v) -> std::complex<float> {
                const float k = 1.0f / 32768.0f;
                return { v.real() * k, v.imag() * k };
            }
        );
    }
};

class ComplexS12SampleAdapter : public SampleAdapter {
public:
	size_t sampleSize() override {
		return sizeof(uint16_t);
	}
	
	void copyRange(const void* const src, size_t start, size_t length, std::complex<float>* const dest) override {
		auto s = reinterpret_cast<const char*>(src);
		char* pSrc = (char*)s + start * 3;
		auto* pDst = (std::complex<float>*)dest;
		
		unsigned long ii;
		unsigned int I;
		
		for (ii = 0; ii < length; ii++) {
			I = *((unsigned int*)pSrc);
			pSrc += 3;
			
			// Real part (first 12 bits)
			float real = (short)((I & 0x0fff) << 4) / 2048.0;
			// Imaginary part (second 12 bits)
			float imag = (short)((I >> 8) & 0xfff0) / 2048.0;
			
			*pDst++ = std::complex<float>(real, imag);
			// *pDst++ = std::complex<float>(imag,real); // or swap ?
		}
	}
};

class ComplexS8SampleAdapter : public SampleAdapter {
public:
    size_t sampleSize() override {
        return sizeof(std::complex<int8_t>);
    }

    void copyRange(const void* const src, size_t start, size_t length, std::complex<float>* const dest) override {
        auto s = reinterpret_cast<const std::complex<int8_t>*>(src);
        std::transform(&s[start], &s[start + length], dest,
            [](const std::complex<int8_t>& v) -> std::complex<float> {
                const float k = 1.0f / 128.0f;
                return { v.real() * k, v.imag() * k };
            }
        );
    }
};

class ComplexU8SampleAdapter : public SampleAdapter {
public:
    size_t sampleSize() override {
        return sizeof(std::complex<uint8_t>);
    }

    void copyRange(const void* const src, size_t start, size_t length, std::complex<float>* const dest) override {
        auto s = reinterpret_cast<const std::complex<uint8_t>*>(src);
        std::transform(&s[start], &s[start + length], dest,
            [](const std::complex<uint8_t>& v) -> std::complex<float> {
                const float k = 1.0f / 128.0f;
                return { (v.real() - 127.4f) * k, (v.imag() - 127.4f) * k };
            }
        );
    }
};

class RealF32SampleAdapter : public SampleAdapter {
public:
    size_t sampleSize() override {
        return sizeof(float);
    }

    void copyRange(const void* const src, size_t start, size_t length, std::complex<float>* const dest) override {
        auto s = reinterpret_cast<const float*>(src);
        std::transform(&s[start], &s[start + length], dest,
            [](const float& v) -> std::complex<float> {
                return {v, 0.0f};
            }
        );
    }
};

class RealF64SampleAdapter : public SampleAdapter {
public:
    size_t sampleSize() override {
        return sizeof(double);
    }

    void copyRange(const void* const src, size_t start, size_t length, std::complex<float>* const dest) override {
        auto s = reinterpret_cast<const double*>(src);
        std::transform(&s[start], &s[start + length], dest,
            [](const double& v) -> std::complex<float> {
                return {static_cast<float>(v), 0.0f};
            }
        );
    }
};

class RealS16SampleAdapter : public SampleAdapter {
public:
    size_t sampleSize() override {
        return sizeof(int16_t);
    }

    void copyRange(const void* const src, size_t start, size_t length, std::complex<float>* const dest) override {
        auto s = reinterpret_cast<const int16_t*>(src);
        std::transform(&s[start], &s[start + length], dest,
            [](const int16_t& v) -> std::complex<float> {
                const float k = 1.0f / 32768.0f;
                return { v * k, 0.0f };
            }
        );
    }
};

class RealS8SampleAdapter : public SampleAdapter {
public:
    size_t sampleSize() override {
        return sizeof(int8_t);
    }

    void copyRange(const void* const src, size_t start, size_t length, std::complex<float>* const dest) override {
        auto s = reinterpret_cast<const int8_t*>(src);
        std::transform(&s[start], &s[start + length], dest,
            [](const int8_t& v) -> std::complex<float> {
                const float k = 1.0f / 128.0f;
                return { v * k, 0.0f };
            }
        );
    }
};

class RealU8SampleAdapter : public SampleAdapter {
public:
    size_t sampleSize() override {
        return sizeof(uint8_t);
    }

    void copyRange(const void* const src, size_t start, size_t length, std::complex<float>* const dest) override {
        auto s = reinterpret_cast<const uint8_t*>(src);
        std::transform(&s[start], &s[start + length], dest,
            [](const uint8_t& v) -> std::complex<float> {
                const float k = 1.0f / 128.0f;
                return { (v - 127.4f) * k, 0 };
            }
        );
    }
};

class RealS12SampleAdapter : public SampleAdapter {
public:
    size_t sampleSize() override {
        return sizeof(uint16_t);
    }

    void copyRange(const void* const src, size_t start, size_t length, std::complex<float>* const dest) override {
        auto s = reinterpret_cast<const char*>(src);
        char* pSrc = (char*)s + start * 3 / 2;
        auto* pDst = (std::complex<float>*)dest;

        unsigned long ii;
        unsigned int I;

        for (ii = 0; ii < length; ii += 2) {
            I = *((unsigned int*)pSrc);
            pSrc += 3;

            *pDst++ = std::complex<float>((short)((I & 0x0fff) << 4) / 2048.0, 0.0f);
            *pDst++ = std::complex<float>((short)((I >> 8) & 0xfff0) / 2048.0, 0.0f);
        }
    }
};

class RealS24SampleAdapter : public SampleAdapter {
public:
    size_t sampleSize() override {
        return 3; // 3 bytes per sample
    }

    void copyRange(const void* const src, size_t start, size_t length, std::complex<float>* const dest) override {
        auto s = reinterpret_cast<const char*>(src);
        char* pSrc = (char*)s + start * 3;
        auto* pDst = (std::complex<float>*)dest;

        for (size_t i = 0; i < length; i++) {
            uint32_t value = *((uint32_t*)pSrc);
            pSrc += 3;

            // Extract 24-bit signed value and extend sign
            int32_t sample = value & 0x00FFFFFF;
            if (sample & 0x00800000) { // Check highest bit (bit 23)
                sample |= 0xFF000000;  // Set all higher bits to 1
            }

            *pDst++ = std::complex<float>(sample / 8388608.0f, 0.0f); // 2^23 for normalization
        }
    }
};

class RealU24SampleAdapter : public SampleAdapter {
public:
    size_t sampleSize() override {
        return 3; // 3 bytes per sample
    }

    void copyRange(const void* const src, size_t start, size_t length, std::complex<float>* const dest) override {
        auto s = reinterpret_cast<const char*>(src);
        char* pSrc = (char*)s + start * 3;
        auto* pDst = (std::complex<float>*)dest;

        for (size_t i = 0; i < length; i++) {
            uint32_t value = *((uint32_t*)pSrc);
            pSrc += 3;

            // Extract 24-bit unsigned value
            uint32_t sample = value & 0x00FFFFFF;
            
            *pDst++ = std::complex<float>(sample / 16777216.0f, 0.0f); // 2^24 for normalization
        }
    }
};

class RealS32SampleAdapter : public SampleAdapter {
public:
    size_t sampleSize() override {
        return sizeof(int32_t);
    }

    void copyRange(const void* const src, size_t start, size_t length, std::complex<float>* const dest) override {
        auto s = reinterpret_cast<const int32_t*>(src);
        std::transform(&s[start], &s[start + length], dest,
            [](const int32_t& v) -> std::complex<float> {
                const float k = 1.0f / 2147483648.0f; // 2^31
                return { v * k, 0.0f };
            }
        );
    }
};

InputSource::InputSource()
{
}

InputSource::~InputSource()
{
    cleanup();
}

void InputSource::cleanup()
{
    if (mmapData != nullptr) {
        inputFile->unmap(mmapData);
        mmapData = nullptr;
    }

    if (inputFile != nullptr) {
        delete inputFile;
        inputFile = nullptr;
    }
}

QJsonObject InputSource::readMetaData(const QString &filename)
{
    QFile datafile(filename);
    if (!datafile.open(QFile::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error("Error while opening meta data file: " + datafile.errorString().toStdString());
    }

    QJsonDocument d = QJsonDocument::fromJson(datafile.readAll());
    datafile.close();
    auto root = d.object();

    if (!root.contains("global") || !root["global"].isObject()) {
        throw std::runtime_error("SigMF meta data is invalid (no global object found)");
    }

    auto global = root["global"].toObject();

    if (!global.contains("core:datatype") || !global["core:datatype"].isString()) {
        throw std::runtime_error("SigMF meta data does not specify a valid datatype");
    }


    auto datatype = global["core:datatype"].toString();
    if (datatype.compare("cf32_le") == 0) {
        sampleAdapter = std::make_unique<ComplexF32SampleAdapter>();
    } else if (datatype.compare("ci32_le") == 0) {
        sampleAdapter = std::make_unique<ComplexS32SampleAdapter>();
    } else if (datatype.compare("ci16_le") == 0) {
        sampleAdapter = std::make_unique<ComplexS16SampleAdapter>();
    } else if (datatype.compare("ci8") == 0) {
        sampleAdapter = std::make_unique<ComplexS8SampleAdapter>();
    } else if (datatype.compare("cu8") == 0) {
        sampleAdapter = std::make_unique<ComplexU8SampleAdapter>();
    } else if (datatype.compare("rf32_le") == 0) {
        sampleAdapter = std::make_unique<RealF32SampleAdapter>();
        _realSignal = true;
    } else if (datatype.compare("ri16_le") == 0) {
        sampleAdapter = std::make_unique<RealS16SampleAdapter>();
        _realSignal = true;
    } else if (datatype.compare("ri8") == 0) {
        sampleAdapter = std::make_unique<RealS8SampleAdapter>();
        _realSignal = true;
    } else if (datatype.compare("ru8") == 0) {
        sampleAdapter = std::make_unique<RealU8SampleAdapter>();
        _realSignal = true;
    } else {
        throw std::runtime_error("SigMF meta data specifies unsupported datatype");
    }

    if (global.contains("core:sample_rate") && global["core:sample_rate"].isDouble()) {
        setSampleRate(global["core:sample_rate"].toDouble());
    }


    if (root.contains("captures") && root["captures"].isArray()) {
        auto captures = root["captures"].toArray();

        for (auto capture_ref : captures) {
            if (capture_ref.isObject()) {
                auto capture = capture_ref.toObject();
                if (capture.contains("core:frequency") && capture["core:frequency"].isDouble()) {
                    frequency = capture["core:frequency"].toDouble();
                }
            } else {
                throw std::runtime_error("SigMF meta data is invalid (invalid capture object)");
            }
        }
    }

    if(root.contains("annotations") && root["annotations"].isArray()) {

        size_t offset = 0;

        if (global.contains("core:offset")) {
            offset = global["offset"].toDouble();
        }

        auto annotations = root["annotations"].toArray();

        for (auto annotation_ref : annotations) {
            if (annotation_ref.isObject()) {
                auto sigmf_annotation = annotation_ref.toObject();

                const size_t sample_start = sigmf_annotation["core:sample_start"].toDouble();

                if (sample_start < offset)
                    continue;

                const size_t rel_sample_start = sample_start - offset;

                const size_t sample_count = sigmf_annotation["core:sample_count"].toDouble();
                auto sampleRange = range_t<size_t>{rel_sample_start, rel_sample_start + sample_count - 1};

                const double freq_lower_edge = sigmf_annotation["core:freq_lower_edge"].toDouble();
                const double freq_upper_edge = sigmf_annotation["core:freq_upper_edge"].toDouble();
                auto frequencyRange = range_t<double>{freq_lower_edge, freq_upper_edge};

                auto label = sigmf_annotation["core:label"].toString();
                if (label.isEmpty()) {
                    label = sigmf_annotation["core:description"].toString();
                }

                auto comment = sigmf_annotation["core:comment"].toString();

                annotationList.emplace_back(sampleRange, frequencyRange, label, comment);
            }
        }
    }

    return root;
}

void InputSource::openFile(const char *filename)
{
    QFileInfo fileInfo(filename);
    std::string suffix = std::string(fileInfo.suffix().toLower().toUtf8().constData());
    if (_fmt != "") { suffix = _fmt; } // allow fmt override
    if ((suffix == "cfile") || (suffix == "cf32")  || (suffix == "fc32")) {
        sampleAdapter = std::make_unique<ComplexF32SampleAdapter>();
    }
    else if ((suffix == "cf64")  || (suffix == "fc64")) {
        sampleAdapter = std::make_unique<ComplexF64SampleAdapter>();
    }
    else if ((suffix == "cs32") || (suffix == "sc32") || (suffix == "c32")) {
        sampleAdapter = std::make_unique<ComplexS32SampleAdapter>();
    }
    else if ((suffix == "cs16") || (suffix == "sc16") || (suffix == "c16")) {
        sampleAdapter = std::make_unique<ComplexS16SampleAdapter>();
    }
    else if ((suffix == "cs8") || (suffix == "sc8") || (suffix == "c8")) {
        sampleAdapter = std::make_unique<ComplexS8SampleAdapter>();
    }
    else if ((suffix == "cu8") || (suffix == "uc8")) {
        sampleAdapter = std::make_unique<ComplexU8SampleAdapter>();
    }
    else if (suffix == "f32") {
        sampleAdapter = std::make_unique<RealF32SampleAdapter>();
        _realSignal = true;
    }
    else if (suffix == "f64") {
        sampleAdapter = std::make_unique<RealF64SampleAdapter>();
        _realSignal = true;
    }
    else if (suffix == "s16") {
        sampleAdapter = std::make_unique<RealS16SampleAdapter>();
        _realSignal = true;
    }
    else if (suffix == "s8") {
        sampleAdapter = std::make_unique<RealS8SampleAdapter>();
        _realSignal = true;
    }
    else if (suffix == "u8") {
        sampleAdapter = std::make_unique<RealU8SampleAdapter>();
        _realSignal = true;
    }
    else if (suffix == "s12" || suffix == "nbl") {
        sampleAdapter = std::make_unique<RealS12SampleAdapter>();
        _realSignal = true;
    }
    else if (suffix == "cs12" || suffix == "sc12") {
        sampleAdapter = std::make_unique<ComplexS12SampleAdapter>();
    }
    else if (suffix == "s24") {
        sampleAdapter = std::make_unique<RealS24SampleAdapter>();
        _realSignal = true;
    }
    else if (suffix == "u24" || suffix == "ui24") {
        sampleAdapter = std::make_unique<RealU24SampleAdapter>();
        _realSignal = true;
    }
    else if (suffix == "sc24" || suffix == "cs24" || suffix == "sdriq") {
        sampleAdapter = std::make_unique<ComplexS24SampleAdapter>();
    }
    else if (suffix == "cu24" || suffix == "uc24") {
        sampleAdapter = std::make_unique<ComplexU24SampleAdapter>();
    }
    else if (suffix == "s32") {
        sampleAdapter = std::make_unique<RealS32SampleAdapter>();
        _realSignal = true;
    }
    else if (suffix == "cs32" || suffix == "sc32") {
        sampleAdapter = std::make_unique<ComplexS32SampleAdapter>();
    }
    else {
        sampleAdapter = std::make_unique<ComplexF32SampleAdapter>();
    }

    QString dataFilename;

    annotationList.clear();
    QString metaFilename;

    if (suffix == "sigmf-meta" || suffix == "sigmf-data" || suffix == "sigmf-") {
        dataFilename = fileInfo.path() + "/" + fileInfo.completeBaseName() + ".sigmf-data";
        metaFilename = fileInfo.path() + "/" + fileInfo.completeBaseName() + ".sigmf-meta";
        auto metaData = readMetaData(metaFilename);
        QFile datafile(dataFilename);
        if (!datafile.open(QFile::ReadOnly | QIODevice::Text)) {
            auto global = metaData["global"].toObject();
            if (global.contains("core:dataset")) {
                auto datasetfilename = global["core:dataset"].toString();
                if(QFileInfo(datasetfilename).isAbsolute()){
                    dataFilename = datasetfilename;
                }
                else{
                    dataFilename = fileInfo.path() + "/" + datasetfilename;
                }
            }
        }
    }
    else if (suffix == "sigmf") {
        throw std::runtime_error("SigMF archives are not supported. Consider extracting a recording.");
    }
    else {
        dataFilename = filename;
    }

    auto file = std::make_unique<QFile>(dataFilename);
    if (!file->open(QFile::ReadOnly)) {
        throw std::runtime_error(file->errorString().toStdString());
    }

    auto size = file->size();
	int offset = 0;

	if(suffix == "sdriq"){
		offset = 32;
		size = size - offset;
	}
	
	sampleCount = size / sampleAdapter->sampleSize();
	
    auto data = file->map(offset, size);
    if (data == nullptr)
        throw std::runtime_error("Error mmapping file");

    cleanup();

    inputFile = file.release();
    mmapData = data;

    invalidate();
}

void InputSource::setSampleRate(double rate)
{
    sampleRate = rate;
    invalidate();
}

double InputSource::rate()
{
    return sampleRate;
}

std::unique_ptr<std::complex<float>[]> InputSource::getSamples(size_t start, size_t length)
{
    if (inputFile == nullptr)
        return nullptr;

    if (mmapData == nullptr)
        return nullptr;

    if(start < 0 || length < 0)
        return nullptr;

    if (start + length > sampleCount)
        return nullptr;

    auto dest = std::make_unique<std::complex<float>[]>(length);
    sampleAdapter->copyRange(mmapData, start, length, dest.get());

    return dest;
}

void InputSource::setFormat(std::string fmt){
    _fmt = fmt;
}
