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

WMFH264Decoder::WMFH264Decoder()
  : mDecoder(nullptr)
{
  memset(&mInputStreamInfo, 0, sizeof(MFT_INPUT_STREAM_INFO));
  memset(&mOutputStreamInfo, 0, sizeof(MFT_OUTPUT_STREAM_INFO));
}

WMFH264Decoder::~WMFH264Decoder()
{
}

HRESULT
WMFH264Decoder::Init()
{
  HMODULE module = ::GetModuleHandle(L"msmpeg2vdec.dll");
  if (!module) {
    assert(module);
    LOG(L"Failed to get msmpeg2vdec.dll\n");
    return E_FAIL;
  }

  typedef HRESULT (WINAPI* DllGetClassObjectFnPtr)(const CLSID& clsid,
                                                   const IID& iid,
                                                   void** object);

  DllGetClassObjectFnPtr GetClassObjPtr =
    reinterpret_cast<DllGetClassObjectFnPtr>(GetProcAddress(module, "DllGetClassObject"));
  if (!GetClassObjPtr) {
    LOG(L"Failed to get DllGetClassObject\n");
    return E_FAIL;
  }

  CComPtr<IClassFactory> classFactory;
  HRESULT hr = GetClassObjPtr(__uuidof(CMSH264DecoderMFT),
                              __uuidof(IClassFactory),
                              reinterpret_cast<void**>(static_cast<IClassFactory**>(&classFactory)));
  if (FAILED(hr)) {
    LOG(L"Failed to get H264 IClassFactory\n");
    return E_FAIL;
  }

  hr = classFactory->CreateInstance(NULL,
                                    __uuidof(IMFTransform),
                                    reinterpret_cast<void**>(static_cast<IMFTransform**>(&mDecoder)));
  if (FAILED(hr)) {
    LOG(L"Failed to get create H264 decoder\n");
    return E_FAIL;
  }

  hr = SetDecoderInputType();
  ENSURE(SUCCEEDED(hr), hr);

  hr = SetDecoderOutputType();
  ENSURE(SUCCEEDED(hr), hr);
  
  hr = SendMFTMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
  ENSURE(SUCCEEDED(hr), hr);

  hr = SendMFTMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
  ENSURE(SUCCEEDED(hr), hr);

  hr = mDecoder->GetInputStreamInfo(0, &mInputStreamInfo);
  ENSURE(SUCCEEDED(hr), hr);

  hr = mDecoder->GetOutputStreamInfo(0, &mOutputStreamInfo);
  ENSURE(SUCCEEDED(hr), hr);

  return S_OK;
}

HRESULT
WMFH264Decoder::ConfigureVideoFrameGeometry(IMFMediaType* aMediaType)
{
  ENSURE(aMediaType != nullptr, E_POINTER);
  HRESULT hr;

  IntRect pictureRegion;
  hr = ::GetPictureRegion(aMediaType, pictureRegion);
  ENSURE(SUCCEEDED(hr), hr);

  UINT32 width = 0, height = 0;
  hr = MFGetAttributeSize(aMediaType, MF_MT_FRAME_SIZE, &width, &height);
  ENSURE(SUCCEEDED(hr), hr);

  // Success! Save state.
  GetDefaultStride(aMediaType, (UINT32*)&mStride);
  mVideoWidth = width;
  mVideoHeight = height;
  mPictureRegion = pictureRegion;

  LOG(L"WMFH264Decoder frame geometry frame=(%u,%u) stride=%u picture=(%d, %d, %d, %d)\n",
      width, height,
      mStride,
      mPictureRegion.x, mPictureRegion.y, mPictureRegion.width, mPictureRegion.height);

  return S_OK;
}

int32_t
WMFH264Decoder::GetFrameWidth() const
{
  return mVideoWidth;
}

int32_t
WMFH264Decoder::GetFrameHeight() const
{
  return mVideoHeight;
}

const IntRect&
WMFH264Decoder::GetPictureRegion() const
{
  return mPictureRegion;
}

int32_t
WMFH264Decoder::GetStride() const
{
  return mStride;
}

HRESULT
WMFH264Decoder::SetDecoderInputType()
{
  HRESULT hr;

  CComPtr<IMFMediaType> type;
  hr = ::MFCreateMediaType(&type);
  ENSURE(SUCCEEDED(hr), hr);

  hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  ENSURE(SUCCEEDED(hr), hr);

  hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
  ENSURE(SUCCEEDED(hr), hr);

  hr = type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_MixedInterlaceOrProgressive);
  ENSURE(SUCCEEDED(hr), hr);
  
  hr = mDecoder->SetInputType(0, type, 0);
  ENSURE(SUCCEEDED(hr), hr);

  return S_OK;
}

HRESULT
WMFH264Decoder::SetDecoderOutputType()
{
  HRESULT hr;

  CComPtr<IMFMediaType> type;

  UINT32 typeIndex = 0;
  while (type = nullptr, SUCCEEDED(mDecoder->GetOutputAvailableType(0, typeIndex++, &type))) {
    GUID subtype;
    hr = type->GetGUID(MF_MT_SUBTYPE, &subtype);
    if (FAILED(hr)) {
      continue;
    }
    if (subtype == MFVideoFormat_I420) {
      hr = mDecoder->SetOutputType(0, type, 0);
      ENSURE(SUCCEEDED(hr), hr);

      hr = ConfigureVideoFrameGeometry(type);
      ENSURE(SUCCEEDED(hr), hr);

      return S_OK;
    }
  }

  return E_FAIL;
}

HRESULT
WMFH264Decoder::SendMFTMessage(MFT_MESSAGE_TYPE aMsg, UINT32 aData)
{
  ENSURE(mDecoder != nullptr, E_POINTER);
  HRESULT hr = mDecoder->ProcessMessage(aMsg, aData);
  ENSURE(SUCCEEDED(hr), hr);
  return S_OK;
}

HRESULT
WMFH264Decoder::CreateInputSample(const uint8_t* aData,
                                  uint32_t aDataSize,
                                  Microseconds aTimestamp,
                                  Microseconds aDuration,
                                  IMFSample** aOutSample)
{
  HRESULT hr;
  CComPtr<IMFSample> sample;
  hr = MFCreateSample(&sample);
  ENSURE(SUCCEEDED(hr), hr);

  CComPtr<IMFMediaBuffer> buffer;
  int32_t bufferSize = std::max<uint32_t>(uint32_t(mInputStreamInfo.cbSize), aDataSize);
  UINT32 alignment = (mInputStreamInfo.cbAlignment > 1) ? mInputStreamInfo.cbAlignment - 1 : 0;
  hr = MFCreateAlignedMemoryBuffer(bufferSize, alignment, &buffer);
  ENSURE(SUCCEEDED(hr), hr);

  DWORD maxLength = 0;
  DWORD currentLength = 0;
  BYTE* dst = nullptr;
  hr = buffer->Lock(&dst, &maxLength, &currentLength);
  ENSURE(SUCCEEDED(hr), hr);

  // Copy data into sample's buffer.
  memcpy(dst, aData, aDataSize);

  hr = buffer->Unlock();
  ENSURE(SUCCEEDED(hr), hr);

  hr = buffer->SetCurrentLength(aDataSize);
  ENSURE(SUCCEEDED(hr), hr);

  hr = sample->AddBuffer(buffer);
  ENSURE(SUCCEEDED(hr), hr);

  hr = sample->SetSampleTime(UsecsToHNs(aTimestamp));
  ENSURE(SUCCEEDED(hr), hr);
  
  sample->SetSampleDuration(UsecsToHNs(aDuration));

  *aOutSample = sample.Detach();

  return S_OK;
}

HRESULT
WMFH264Decoder::CreateOutputSample(IMFSample** aOutSample)
{
  HRESULT hr;
  CComPtr<IMFSample> sample;
  hr = MFCreateSample(&sample);
  ENSURE(SUCCEEDED(hr), hr);

  CComPtr<IMFMediaBuffer> buffer;
  int32_t bufferSize = mOutputStreamInfo.cbSize;
  UINT32 alignment = (mOutputStreamInfo.cbAlignment > 1) ? mOutputStreamInfo.cbAlignment - 1 : 0;
  hr = MFCreateAlignedMemoryBuffer(bufferSize, alignment, &buffer);
  ENSURE(SUCCEEDED(hr), hr);

  DWORD maxLength = 0;
  DWORD currentLength = 0;
  BYTE* dst = nullptr;

  hr = sample->AddBuffer(buffer);
  ENSURE(SUCCEEDED(hr), hr);

  *aOutSample = sample.Detach();

  return S_OK;
}


HRESULT
WMFH264Decoder::GetOutputSample(IMFSample** aOutSample)
{
  HRESULT hr;
  // We allocate samples for MFT output.
  MFT_OUTPUT_DATA_BUFFER output = {0};

  CComPtr<IMFSample> sample;
  hr = CreateOutputSample(&sample);
  ENSURE(SUCCEEDED(hr), hr);

  output.pSample = sample;

  DWORD status = 0;
  hr = mDecoder->ProcessOutput(0, 1, &output, &status);
  //LOG(L"WMFH264Decoder::GetOutputSample() ProcessOutput returned 0x%x\n", hr);
  CComPtr<IMFCollection> events = output.pEvents; // Ensure this is released.

  if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
    // Type change. Probably geometric apperature change.
    hr = SetDecoderOutputType();
    ENSURE(SUCCEEDED(hr), hr);

    return GetOutputSample(aOutSample);  
  } else if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT || !sample) {
    return MF_E_TRANSFORM_NEED_MORE_INPUT;
  }
  // Treat other errors as fatal.
  ENSURE(SUCCEEDED(hr), hr);

  assert(sample);

  // output.pSample
  *aOutSample = sample.Detach(); // AddRefs
  return S_OK;
}

HRESULT
WMFH264Decoder::Input(const uint8_t* aData,
                      uint32_t aDataSize,
                      Microseconds aTimestamp,
                      Microseconds aDuration)
{
  HRESULT hr;
  CComPtr<IMFSample> input = nullptr;
  hr = CreateInputSample(aData, aDataSize, aTimestamp, aDuration, &input);
  ENSURE(SUCCEEDED(hr) && input!=nullptr, hr);

  hr = mDecoder->ProcessInput(0, input, 0);
  if (hr == MF_E_NOTACCEPTING) {
    // MFT *already* has enough data to produce a sample. Retrieve it.
    LOG(L"ProcessInput returned MF_E_NOTACCEPTING\n");
    return MF_E_NOTACCEPTING;
  }
  ENSURE(SUCCEEDED(hr), hr);

  return S_OK;
}

HRESULT
WMFH264Decoder::Output(IMFSample** aOutput)
{
  HRESULT hr;
  CComPtr<IMFSample> outputSample;
  hr = GetOutputSample(&outputSample);
  if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
    return MF_E_TRANSFORM_NEED_MORE_INPUT;
  }
  // Treat other errors as fatal.
  ENSURE(SUCCEEDED(hr) && outputSample, hr);

  *aOutput = outputSample.Detach();

  return S_OK;
}

HRESULT
WMFH264Decoder::Reset()
{
  HRESULT hr = SendMFTMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
  ENSURE(SUCCEEDED(hr), hr);

  return S_OK;
}
 
