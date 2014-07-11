/*
 * Copyright 2013, Mozilla Foundation and contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "stdafx.h"

class VideoDecoder : public GMPVideoDecoder
{
public:
  VideoDecoder(GMPVideoHost *aHostAPI);

  virtual ~VideoDecoder();

  virtual GMPErr InitDecode(const GMPVideoCodec& aCodecSettings,
                            const uint8_t* aCodecSpecific,
                            uint32_t aCodecSpecificLength,
                            GMPVideoDecoderCallback* aCallback,
                            int32_t aCoreCount) override;

  virtual GMPErr Decode(GMPVideoEncodedFrame* aInputFrame,
                        bool aMissingFrames,
                        const uint8_t* aCodecSpecific,
                        uint32_t aCodecSpecificLength,
                        int64_t aRenderTimeMs = -1);

  virtual GMPErr Reset() override;

  virtual GMPErr Drain() override;

  virtual void DecodingComplete() override;

private:

  void DecodeTask(GMPVideoEncodedFrame* aInputFrame);

  void ReturnOutput(IMFSample* aSample);

  HRESULT SampleToVideoFrame(IMFSample* aSample,
                             GMPVideoi420Frame* aVideoFrame);

  GMPVideoHost *mHostAPI; // host-owned, invalid at DecodingComplete
  GMPVideoDecoderCallback* mCallback; // host-owned, invalid at DecodingComplete
  AutoPtr<GMPThread> mWorkerThread;
  AutoPtr<GMPMutex> mMutex;
  AutoPtr<WMFH264Decoder> mDecoder;

  std::vector<uint8_t> mExtraData;
  AVCDecoderConfigurationRecord mAVCC;
  std::vector<uint8_t> mAnnexB;

  int32_t mNumInputTasks;
  bool mSentExtraData;
};
