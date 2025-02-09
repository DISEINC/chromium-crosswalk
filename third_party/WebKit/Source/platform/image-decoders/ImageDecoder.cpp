/*
 * Copyright (C) Research In Motion Limited 2009-2010. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 *
 */

#include "platform/image-decoders/ImageDecoder.h"

#include "platform/PlatformInstrumentation.h"
#include "platform/graphics/DeferredImageDecoder.h"
#include "platform/image-decoders/bmp/BMPImageDecoder.h"
#include "platform/image-decoders/gif/GIFImageDecoder.h"
#include "platform/image-decoders/ico/ICOImageDecoder.h"
#include "platform/image-decoders/jpeg/JPEGImageDecoder.h"
#include "platform/image-decoders/png/PNGImageDecoder.h"
#include "platform/image-decoders/webp/WEBPImageDecoder.h"
#include "wtf/PassOwnPtr.h"

namespace blink {

inline bool matchesJPEGSignature(const char* contents)
{
    return !memcmp(contents, "\xFF\xD8\xFF", 3);
}

inline bool matchesPNGSignature(const char* contents)
{
    return !memcmp(contents, "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A", 8);
}

inline bool matchesGIFSignature(const char* contents)
{
    return !memcmp(contents, "GIF87a", 6) || !memcmp(contents, "GIF89a", 6);
}

inline bool matchesWebPSignature(const char* contents)
{
    return !memcmp(contents, "RIFF", 4) && !memcmp(contents + 8, "WEBPVP", 6);
}

inline bool matchesICOSignature(const char* contents)
{
    return !memcmp(contents, "\x00\x00\x01\x00", 4);
}

inline bool matchesCURSignature(const char* contents)
{
    return !memcmp(contents, "\x00\x00\x02\x00", 4);
}

inline bool matchesBMPSignature(const char* contents)
{
    return !memcmp(contents, "BM", 2);
}

PassOwnPtr<ImageDecoder> ImageDecoder::create(const char* contents, size_t length, AlphaOption alphaOption, GammaAndColorProfileOption colorOptions)
{
    const size_t longestSignatureLength = sizeof("RIFF????WEBPVP") - 1;
    ASSERT(longestSignatureLength == 14);

    if (length < longestSignatureLength)
        return nullptr;

    size_t maxDecodedBytes = Platform::current() ? Platform::current()->maxDecodedImageBytes() : noDecodedImageByteLimit;

    if (matchesJPEGSignature(contents))
        return adoptPtr(new JPEGImageDecoder(alphaOption, colorOptions, maxDecodedBytes));

    if (matchesPNGSignature(contents))
        return adoptPtr(new PNGImageDecoder(alphaOption, colorOptions, maxDecodedBytes));

    if (matchesGIFSignature(contents))
        return adoptPtr(new GIFImageDecoder(alphaOption, colorOptions, maxDecodedBytes));

    if (matchesWebPSignature(contents))
        return adoptPtr(new WEBPImageDecoder(alphaOption, colorOptions, maxDecodedBytes));

    if (matchesICOSignature(contents) || matchesCURSignature(contents))
        return adoptPtr(new ICOImageDecoder(alphaOption, colorOptions, maxDecodedBytes));

    if (matchesBMPSignature(contents))
        return adoptPtr(new BMPImageDecoder(alphaOption, colorOptions, maxDecodedBytes));

    return nullptr;
}

PassOwnPtr<ImageDecoder> ImageDecoder::create(const SharedBuffer& data, AlphaOption alphaOption, GammaAndColorProfileOption colorOptions)
{
    const char* contents;
    const size_t length = data.getSomeData<size_t>(contents);
    return create(contents, length, alphaOption, colorOptions);
}

PassOwnPtr<ImageDecoder> ImageDecoder::create(const SegmentReader& data, AlphaOption alphaOption, GammaAndColorProfileOption colorOptions)
{
    const char* contents;
    const size_t length = data.getSomeData(contents, 0);
    return create(contents, length, alphaOption, colorOptions);
}

size_t ImageDecoder::frameCount()
{
    const size_t oldSize = m_frameBufferCache.size();
    const size_t newSize = decodeFrameCount();
    if (oldSize != newSize) {
        m_frameBufferCache.resize(newSize);
        for (size_t i = oldSize; i < newSize; ++i) {
            m_frameBufferCache[i].setPremultiplyAlpha(m_premultiplyAlpha);
            initializeNewFrame(i);
        }
    }
    return newSize;
}

ImageFrame* ImageDecoder::frameBufferAtIndex(size_t index)
{
    if (index >= frameCount())
        return 0;

    ImageFrame* frame = &m_frameBufferCache[index];
    if (frame->getStatus() != ImageFrame::FrameComplete) {
        PlatformInstrumentation::willDecodeImage(filenameExtension());
        decode(index);
        PlatformInstrumentation::didDecodeImage();
    }

    frame->notifyBitmapIfPixelsChanged();
    return frame;
}

bool ImageDecoder::frameHasAlphaAtIndex(size_t index) const
{
    return !frameIsCompleteAtIndex(index) || m_frameBufferCache[index].hasAlpha();
}

bool ImageDecoder::frameIsCompleteAtIndex(size_t index) const
{
    return (index < m_frameBufferCache.size()) &&
        (m_frameBufferCache[index].getStatus() == ImageFrame::FrameComplete);
}

size_t ImageDecoder::frameBytesAtIndex(size_t index) const
{
    if (index >= m_frameBufferCache.size() || m_frameBufferCache[index].getStatus() == ImageFrame::FrameEmpty)
        return 0;

    struct ImageSize {

        explicit ImageSize(IntSize size)
        {
            area = static_cast<uint64_t>(size.width()) * size.height();
        }

        uint64_t area;
    };

    return ImageSize(frameSizeAtIndex(index)).area * sizeof(ImageFrame::PixelData);
}

bool ImageDecoder::deferredImageDecodingEnabled()
{
    return DeferredImageDecoder::enabled();
}

size_t ImageDecoder::clearCacheExceptFrame(size_t clearExceptFrame)
{
    // Don't clear if there are no frames or only one frame.
    if (m_frameBufferCache.size() <= 1)
        return 0;

    size_t frameBytesCleared = 0;
    for (size_t i = 0; i < m_frameBufferCache.size(); ++i) {
        if (i != clearExceptFrame) {
            frameBytesCleared += frameBytesAtIndex(i);
            clearFrameBuffer(i);
        }
    }
    return frameBytesCleared;
}

void ImageDecoder::clearFrameBuffer(size_t frameIndex)
{
    m_frameBufferCache[frameIndex].clearPixelData();
}

size_t ImageDecoder::findRequiredPreviousFrame(size_t frameIndex, bool frameRectIsOpaque)
{
    ASSERT(frameIndex <= m_frameBufferCache.size());
    if (!frameIndex) {
        // The first frame doesn't rely on any previous data.
        return kNotFound;
    }

    const ImageFrame* currBuffer = &m_frameBufferCache[frameIndex];
    if ((frameRectIsOpaque || currBuffer->getAlphaBlendSource() == ImageFrame::BlendAtopBgcolor)
        && currBuffer->originalFrameRect().contains(IntRect(IntPoint(), size())))
        return kNotFound;

    // The starting state for this frame depends on the previous frame's
    // disposal method.
    size_t prevFrame = frameIndex - 1;
    const ImageFrame* prevBuffer = &m_frameBufferCache[prevFrame];

    switch (prevBuffer->getDisposalMethod()) {
    case ImageFrame::DisposeNotSpecified:
    case ImageFrame::DisposeKeep:
        // prevFrame will be used as the starting state for this frame.
        // FIXME: Be even smarter by checking the frame sizes and/or alpha-containing regions.
        return prevFrame;
    case ImageFrame::DisposeOverwritePrevious:
        // Frames that use the DisposeOverwritePrevious method are effectively
        // no-ops in terms of changing the starting state of a frame compared to
        // the starting state of the previous frame, so skip over them and
        // return the required previous frame of it.
        return prevBuffer->requiredPreviousFrameIndex();
    case ImageFrame::DisposeOverwriteBgcolor:
        // If the previous frame fills the whole image, then the current frame
        // can be decoded alone. Likewise, if the previous frame could be
        // decoded without reference to any prior frame, the starting state for
        // this frame is a blank frame, so it can again be decoded alone.
        // Otherwise, the previous frame contributes to this frame.
        return (prevBuffer->originalFrameRect().contains(IntRect(IntPoint(), size()))
            || (prevBuffer->requiredPreviousFrameIndex() == kNotFound)) ? kNotFound : prevFrame;
    default:
        ASSERT_NOT_REACHED();
        return kNotFound;
    }
}

ImagePlanes::ImagePlanes()
{
    for (int i = 0; i < 3; ++i) {
        m_planes[i] = 0;
        m_rowBytes[i] = 0;
    }
}

ImagePlanes::ImagePlanes(void* planes[3], const size_t rowBytes[3])
{
    for (int i = 0; i < 3; ++i) {
        m_planes[i] = planes[i];
        m_rowBytes[i] = rowBytes[i];
    }
}

void* ImagePlanes::plane(int i)
{
    ASSERT((i >= 0) && i < 3);
    return m_planes[i];
}

size_t ImagePlanes::rowBytes(int i) const
{
    ASSERT((i >= 0) && i < 3);
    return m_rowBytes[i];
}

bool ImageDecoder::hasColorProfile() const
{
#if USE(QCMSLIB)
    return m_sourceToOutputDeviceColorTransform.get();
#else
    return false;
#endif
}

#if USE(QCMSLIB)
namespace {

const unsigned kIccColorProfileHeaderLength = 128;

bool rgbColorProfile(const char* profileData, unsigned profileLength)
{
    ASSERT_UNUSED(profileLength, profileLength >= kIccColorProfileHeaderLength);

    return !memcmp(&profileData[16], "RGB ", 4);
}

bool inputDeviceColorProfile(const char* profileData, unsigned profileLength)
{
    ASSERT_UNUSED(profileLength, profileLength >= kIccColorProfileHeaderLength);

    return !memcmp(&profileData[12], "mntr", 4) || !memcmp(&profileData[12], "scnr", 4);
}

// The output device color profile is global and shared across multiple threads.
SpinLock gOutputDeviceProfileLock;
qcms_profile* gOutputDeviceProfile = nullptr;

} // namespace

// static
void ImageDecoder::setColorProfileAndTransform(const char* iccData, unsigned iccLength, bool hasAlpha, bool useSRGB)
{
    m_sourceToOutputDeviceColorTransform.clear();

    // Create the input profile
    OwnPtr<qcms_profile> inputProfile;
    if (useSRGB) {
        inputProfile = adoptPtr(qcms_profile_sRGB());
    } else {
        // Only accept RGB color profiles from input class devices.
        if (iccLength < kIccColorProfileHeaderLength)
            return;
        if (!rgbColorProfile(iccData, iccLength))
            return;
        if (!inputDeviceColorProfile(iccData, iccLength))
            return;
        inputProfile = adoptPtr(qcms_profile_from_memory(iccData, iccLength));
    }
    if (!inputProfile)
        return;

    // We currently only support color profiles for RGB profiled images.
    ASSERT(rgbData == qcms_profile_get_color_space(inputProfile.get()));

    // Take a lock around initializing and accessing the global device color profile.
    SpinLock::Guard guard(gOutputDeviceProfileLock);

    // Initialize the output device profile.
    if (!gOutputDeviceProfile) {
        // FIXME: Add optional ICCv4 support and support for multiple monitors.
        WebVector<char> profile;
        Platform::current()->screenColorProfile(&profile);

        if (!profile.isEmpty())
            gOutputDeviceProfile = qcms_profile_from_memory(profile.data(), profile.size());

        if (gOutputDeviceProfile && qcms_profile_is_bogus(gOutputDeviceProfile)) {
            qcms_profile_release(gOutputDeviceProfile);
            gOutputDeviceProfile = nullptr;
        }

        if (!gOutputDeviceProfile)
            gOutputDeviceProfile = qcms_profile_sRGB();

        qcms_profile_precache_output_transform(gOutputDeviceProfile);
    }

    if (qcms_profile_match(inputProfile.get(), gOutputDeviceProfile))
        return;

    qcms_data_type dataFormat = hasAlpha ? QCMS_DATA_RGBA_8 : QCMS_DATA_RGB_8;

    // FIXME: Don't force perceptual intent if the image profile contains an intent.
    m_sourceToOutputDeviceColorTransform = adoptPtr(qcms_transform_create(inputProfile.get(), dataFormat, gOutputDeviceProfile, QCMS_DATA_RGBA_8, QCMS_INTENT_PERCEPTUAL));
}

#endif // USE(QCMSLIB)

} // namespace blink
