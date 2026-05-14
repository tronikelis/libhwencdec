#ifndef LIBHWENCDEC_INCLUDE_DECODER_H
#define LIBHWENCDEC_INCLUDE_DECODER_H

#include <cuviddec.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <cuda.h>
#include <npp.h>
#include <nvcuvid.h>

typedef struct _DecodedFrame {
    void* data;
    uint32_t len;
} DecodedFrame;

typedef struct _NvDecodeSession {
    CUvideodecoder decoder;
    CUvideoparser parser;
    CUcontext cu_context;

    uint32_t width;
    uint32_t height;

    DecodedFrame latest_decoded_frame;
} NvDecodeSession;

int DecodedFrame_new(DecodedFrame* self, unsigned long long cu_dev_ptr,
                     uint32_t len);
int DecodedFrame_destroy(DecodedFrame self);

int NvDecodeSession_decode_frame(NvDecodeSession* self, void* frame,
                                 uint32_t frame_len,
                                 DecodedFrame* out_decoded_frame);
int NvDecodeSession_new_h264(NvDecodeSession* self, uint32_t width,
                             uint32_t height);
int NvDecodeSession_destroy(NvDecodeSession self);
void NvDecodeSession_set_latest_decoded_frame(NvDecodeSession* self,
                                              DecodedFrame frame);

#ifdef LIBHWENCDEC_DECODER_IMPLEMENTATION

void nv12_to_rgba(uint8_t* nv12, uint32_t len, uint8_t** out_rgba,
                  uint32_t* out_len) {}

int DecodedFrame_new(DecodedFrame* self, unsigned long long cu_dev_ptr,
                     uint32_t len) {
    DecodedFrame _tmp = {0};
    *self = _tmp;

    CUresult cu_result;

    cu_result = cuMemAllocHost(&self->data, len);
    if (cu_result != 0) {
        return 1;
    }

    cu_result = cuMemcpyDtoH(self->data, cu_dev_ptr, len);
    if (cu_result != 0) {
        return 1;
    }

    self->len = len;

    return 0;
}

int DecodedFrame_destroy(DecodedFrame self) {
    CUresult cu_result;
    cu_result = cuMemFreeHost(self.data);
    if (cu_result != 0) {
        return 1;
    }
    return 0;
}

void NvDecodeSession_set_latest_decoded_frame(NvDecodeSession* self,
                                              DecodedFrame frame) {
    if (self->latest_decoded_frame.data != NULL) {
        DecodedFrame_destroy(self->latest_decoded_frame);
    }
    self->latest_decoded_frame = frame;
}

int NvDecodeSession_create_decoder_if_null(NvDecodeSession* self,
                                           CUVIDEOFORMAT* from_video_format) {
    if (self->decoder != NULL) {
        return 0;
    }

    CUresult cu_result;

    CUVIDDECODECREATEINFO decode_create_info = {0};
    decode_create_info.CodecType = from_video_format->codec;
    decode_create_info.ulWidth = from_video_format->coded_width;
    decode_create_info.ulHeight = from_video_format->coded_height;
    decode_create_info.ChromaFormat = from_video_format->chroma_format;
    decode_create_info.ulNumDecodeSurfaces =
        from_video_format->min_num_decode_surfaces;
    decode_create_info.ulNumOutputSurfaces = 8;
    decode_create_info.OutputFormat = cudaVideoSurfaceFormat_NV12;
    decode_create_info.ulTargetWidth = decode_create_info.ulWidth;
    decode_create_info.ulTargetHeight = decode_create_info.ulHeight;
    decode_create_info.DeinterlaceMode = cudaVideoDeinterlaceMode_Weave;

    cu_result = cuvidCreateDecoder(&self->decoder, &decode_create_info);
    if (cu_result != 0) {
        printf("failed creating decoder cu_result: %d\n", cu_result);
        return 1;
    }

    return 0;
}

int NvDecodeSession_pfnSequenceCallback(void* _self,
                                        CUVIDEOFORMAT* video_format) {
    NvDecodeSession* self = _self;

    printf("sequence change\n");

    if (NvDecodeSession_create_decoder_if_null(self, video_format) != 0) {
        return 0;
    }

    return video_format->min_num_decode_surfaces;
}

int NvDecodeSession_pfnDecodePicture(void* _self, CUVIDPICPARAMS* pic_params) {
    NvDecodeSession* self = _self;
    printf("decode picture\n");

    CUresult cu_result;

    printf("CurrPicIdx: %d, bitstreamdatalen: %d, numslices: %d\n",
           pic_params->CurrPicIdx, pic_params->nBitstreamDataLen,
           pic_params->nNumSlices);

    if (self->decoder == NULL) {
        printf("decoder NULL\n");
        return 0;
    }

    cu_result = cuvidDecodePicture(self->decoder, pic_params);
    if (cu_result != 0) {
        printf("failed decoding picture, cu_result: %d\n", cu_result);
        return 0;
    }

    CUVIDPROCPARAMS proc_params = {0};
    unsigned long long cu_dev_ptr;
    unsigned int pitch;
    cu_result = cuvidMapVideoFrame(self->decoder, pic_params->CurrPicIdx,
                                   &cu_dev_ptr, &pitch, &proc_params);
    if (cu_result != 0) {
        printf("failed mapping video frame, cu_result: %d\n", cu_result);
        return 0;
    }

    printf("mapped video frame, p_dev_ptr: %llu, p_pitch: %d\n", cu_dev_ptr,
           pitch);

    void* frame = NULL;
    uint32_t frame_size = self->height * pitch;
    frame_size += frame_size / 2;

    DecodedFrame decoded_frame;
    if (DecodedFrame_new(&decoded_frame, cu_dev_ptr, frame_size) != 0) {
        printf("failed creating decoded frame\n");
        return 0;
    }
    NvDecodeSession_set_latest_decoded_frame(self, decoded_frame);

    cu_result = cuvidUnmapVideoFrame(self->decoder, cu_dev_ptr);
    if (cu_result != 0) {
        printf("failed unmaping video frame, cu_result: %d\n", cu_result);
        return 0;
    }

    printf("unmapped video frame\n");

    return 1;
}

int NvDecodeSession_pfnDisplayPicture(void* _self,
                                      CUVIDPARSERDISPINFO* disp_info) {
    NvDecodeSession* self = _self;

    CUresult cu_result;

    printf("display picture, idx: %d\n", disp_info->picture_index);

    return 1;
}

int NvDecodeSession_decode_frame(NvDecodeSession* self, void* frame,
                                 uint32_t frame_len,
                                 DecodedFrame* out_decoded_frame) {
    CUresult cu_result;

    CUVIDSOURCEDATAPACKET source_data_packet = {0};
    source_data_packet.flags = CUVID_PKT_ENDOFPICTURE | CUVID_PKT_NOTIFY_EOS;
    source_data_packet.payload = frame;
    source_data_packet.payload_size = frame_len;
    cu_result = cuvidParseVideoData(self->parser, &source_data_packet);
    if (cu_result != 0) {
        printf("failed parsing video data, cu_result: %d\n", cu_result);
        return 1;
    }

    *out_decoded_frame = self->latest_decoded_frame;

    return 0;
}

int NvDecodeSession_new_h264(NvDecodeSession* self, uint32_t width,
                             uint32_t height) {
    CUresult cu_result;

    NvDecodeSession _tmp = {0};
    *self = _tmp;

    self->width = width;
    self->height = height;

    CUcontext cu_context;
    cu_result = cuCtxCreate(&cu_context, NULL, 0, 0);
    if (cu_result != 0) {
        return 1;
    }
    self->cu_context = cu_context;

    CUVIDPARSERPARAMS parser_params = {0};
    parser_params.CodecType = cudaVideoCodec_H264;
    parser_params.ulMaxNumDecodeSurfaces = 1;
    parser_params.ulErrorThreshold = 100;
    parser_params.pUserData = self;
    parser_params.pfnSequenceCallback = NvDecodeSession_pfnSequenceCallback;
    parser_params.pfnDecodePicture = NvDecodeSession_pfnDecodePicture;
    parser_params.pfnDisplayPicture = NvDecodeSession_pfnDisplayPicture;
    cu_result = cuvidCreateVideoParser(&self->parser, &parser_params);
    if (cu_result != 0) {
        printf("failed creating video parser, cu_result: %d\n", cu_result);
        return 1;
    }

    return 0;
}

int NvDecodeSession_destroy(NvDecodeSession self) {
    CUresult cu_result;

    cu_result = cuCtxDestroy(self.cu_context);
    if (cu_result != 0) {
        return 1;
    }

    return 0;
}

#endif
#endif
