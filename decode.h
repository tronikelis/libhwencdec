#ifndef LIBHWENCDEC_INCLUDE_DECODER_H
#define LIBHWENCDEC_INCLUDE_DECODER_H

#include <cuviddec.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <cuda.h>
#include <nvcuvid.h>

typedef struct _NvDecodeSession {
    CUvideodecoder decoder;
    CUvideoparser parser;
    CUcontext cu_context;

    uint32_t width;
    uint32_t height;
} NvDecodeSession;

int NvDecodeSession_parse_video_data(NvDecodeSession* self, void* frame,
                                     uint32_t frame_len);
int NvDecodeSession_new_h264(NvDecodeSession* self, uint32_t width,
                             uint32_t height);
int NvDecodeSession_decode_frame(NvDecodeSession* self, void* frame,
                                 uint32_t frame_len);
int NvDecodeSession_destroy(NvDecodeSession self);

#ifdef LIBHWENCDEC_DECODER_IMPLEMENTATION

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
    decode_create_info.OutputFormat = cudaVideoSurfaceFormat_YUV444;
    decode_create_info.ulTargetWidth = decode_create_info.ulWidth;
    decode_create_info.ulTargetHeight = decode_create_info.ulHeight;
    decode_create_info.DeinterlaceMode = cudaVideoDeinterlaceMode_Weave;

    cu_result = cuvidCreateDecoder(&self->decoder, &decode_create_info);
    if (cu_result != 0) {
        return 1;
    }

    return 0;
}

int NvDecodeSession_pfnSequenceCallback(void* _self,
                                        CUVIDEOFORMAT* video_format) {
    NvDecodeSession* self = _self;

    printf("sequence change\n");

    if (NvDecodeSession_create_decoder_if_null(self, video_format) != 0) {
        return 1;
    }

    return video_format->min_num_decode_surfaces;
}

int NvDecodeSession_pfnDecodePicture(void* _self, CUVIDPICPARAMS* pic_params) {
    NvDecodeSession* self = _self;

    printf("decode picture\n");

    printf("CurrPicIdx: %d, bitstreamdatalen: %d, numslices: %d\n",
           pic_params->CurrPicIdx, pic_params->nBitstreamDataLen,
           pic_params->nNumSlices);

    return 1;
}

int NvDecodeSession_pfnDisplayPicture(void* _self,
                                      CUVIDPARSERDISPINFO* pic_params) {
    NvDecodeSession* self = _self;

    printf("display picture\n");

    return 1;
}

int NvDecodeSession_parse_video_data(NvDecodeSession* self, void* frame,
                                     uint32_t frame_len) {
    CUresult cu_result;

    CUVIDSOURCEDATAPACKET source_data_packet = {0};
    source_data_packet.flags = CUVID_PKT_ENDOFPICTURE | CUVID_PKT_NOTIFY_EOS;
    source_data_packet.payload = frame;
    source_data_packet.payload_size = frame_len;
    cu_result = cuvidParseVideoData(self->parser, &source_data_packet);
    if (cu_result != 0) {
        return 1;
    }

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
        return 1;
    }

    return 0;
}

int NvDecodeSession_decode_frame(NvDecodeSession* self, void* frame,
                                 uint32_t frame_len) {
    CUresult cu_result;

    CUVIDPICPARAMS cuvid_pic_params = {0};
    cuvid_pic_params.pBitstreamData = frame;
    cuvid_pic_params.nBitstreamDataLen = frame_len;
    cu_result = cuvidDecodePicture(self->decoder, &cuvid_pic_params);
    if (cu_result != 0) {
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
