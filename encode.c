#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cuda.h>
#include <nvEncodeAPI.h>

typedef struct _NvEncodeSession {
    void* encoder;
    CUcontext cu_context;
    NV_ENCODE_API_FUNCTION_LIST fns;
    NV_ENC_INPUT_PTR input_buffer;
    NV_ENC_OUTPUT_PTR bitstream_buffer;

    uint32_t width;
    uint32_t height;
    uint32_t framerate_num;
    uint32_t framerate_den;

    NV_ENC_BUFFER_FORMAT buffer_format;
} NvEncodeSession;

typedef struct _EncodedFrame {
    void* data;
    uint32_t len;
} EncodedFrame;

void EncodedFrame_new(EncodedFrame* self, void* data, uint32_t len) {
    EncodedFrame _tmp = {0};
    *self = _tmp;

    self->data = malloc(len);
    self->len = len;
    memcpy(self->data, data, len);
}

void EncodedFrame_destroy(EncodedFrame self) { free(self.data); }

void NvEncodeSession_print_last_error(NvEncodeSession* self) {
    const char* str = self->fns.nvEncGetLastErrorString(self->encoder);
    printf("error: %s\n", str);
}

int NvEncodeSession_destroy(NvEncodeSession self) {
    NVENCSTATUS nv_status;
    CUresult cu_result;

    if (self.input_buffer != NULL) {
        nv_status =
            self.fns.nvEncDestroyInputBuffer(self.encoder, self.input_buffer);
        if (nv_status != 0) {
            return 1;
        }
    }
    if (self.bitstream_buffer != NULL) {
        nv_status = self.fns.nvEncDestroyBitstreamBuffer(self.encoder,
                                                         self.bitstream_buffer);
        if (nv_status != 0) {
            return 1;
        }
    }

    nv_status = self.fns.nvEncDestroyEncoder(self.encoder);
    if (nv_status != 0) {
        return 1;
    }
    cu_result = cuCtxDestroy(self.cu_context);
    if (cu_result != 0) {
        return 1;
    }

    return 0;
}

int NvEncodeSession_encode_frame(NvEncodeSession* self, void* frame_input,
                                 EncodedFrame* out_encoded_frame) {
    NVENCSTATUS nv_status;

    NV_ENC_LOCK_INPUT_BUFFER lock_input_buffer_params = {0};
    lock_input_buffer_params.version = NV_ENC_LOCK_INPUT_BUFFER_VER;
    lock_input_buffer_params.inputBuffer = self->input_buffer;
    nv_status = self->fns.nvEncLockInputBuffer(self->encoder,
                                               &lock_input_buffer_params);
    if (nv_status != 0) {
        printf("failed to lock input buffer: %d\n", nv_status);
        return 1;
    }

    int c = 0;
    switch (self->buffer_format) {
    case NV_ENC_BUFFER_FORMAT_ARGB:
        c = 4;
        break;
    default:
        return 1;
    }

    memcpy(lock_input_buffer_params.bufferDataPtr, frame_input,
           self->width * self->height * c);

    nv_status =
        self->fns.nvEncUnlockInputBuffer(self->encoder, self->input_buffer);
    if (nv_status != 0) {
        printf("failed to unlock input buffer: %d\n", nv_status);
        return 1;
    }

    NV_ENC_PIC_PARAMS enc_pic_params = {0};
    enc_pic_params.version = NV_ENC_PIC_PARAMS_VER;
    enc_pic_params.inputBuffer = self->input_buffer;
    enc_pic_params.outputBitstream = self->bitstream_buffer;
    enc_pic_params.inputWidth = self->width;
    enc_pic_params.inputHeight = self->height;
    enc_pic_params.bufferFmt = self->buffer_format;
    enc_pic_params.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    enc_pic_params.encodePicFlags = NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
    nv_status = self->fns.nvEncEncodePicture(self->encoder, &enc_pic_params);
    if (nv_status != 0) {
        return 1;
    }

    NV_ENC_LOCK_BITSTREAM lock_bitstream_params = {0};
    lock_bitstream_params.version = NV_ENC_LOCK_BITSTREAM_VER;
    lock_bitstream_params.outputBitstream = self->bitstream_buffer;
    nv_status =
        self->fns.nvEncLockBitstream(self->encoder, &lock_bitstream_params);
    if (nv_status != 0) {
        return 1;
    }

    EncodedFrame_new(out_encoded_frame,
                     lock_bitstream_params.bitstreamBufferPtr,
                     lock_bitstream_params.bitstreamSizeInBytes);

    nv_status =
        self->fns.nvEncUnlockBitstream(self->encoder, self->bitstream_buffer);
    if (nv_status != 0) {
        return 1;
    }

    return 0;
}

int NvEncodeSession_init_h264(NvEncodeSession* self,
                              NV_ENC_BUFFER_FORMAT* wanted_formats,
                              int wanted_formats_len,
                              NV_ENC_BUFFER_FORMAT* out_selected_format) {
    NVENCSTATUS nv_status;

    NV_ENC_INITIALIZE_PARAMS initialize_params = {0};
    initialize_params.version = NV_ENC_INITIALIZE_PARAMS_VER;
    initialize_params.encodeGUID = NV_ENC_CODEC_H264_GUID;
    initialize_params.presetGUID = NV_ENC_PRESET_P2_GUID;
    initialize_params.tuningInfo = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
    initialize_params.encodeWidth = self->width;
    initialize_params.encodeHeight = self->height;
    initialize_params.frameRateNum = self->framerate_num;
    initialize_params.frameRateDen = self->framerate_den;
    initialize_params.enablePTD = 1;

    NV_ENC_CONFIG enc_config = {0};
    enc_config.version = NV_ENC_CONFIG_VER;
    NV_ENC_PRESET_CONFIG preset_config = {0};
    preset_config.version = NV_ENC_PRESET_CONFIG_VER;
    preset_config.presetCfg = enc_config;

    nv_status = self->fns.nvEncGetEncodePresetConfigEx(
        self->encoder, initialize_params.encodeGUID,
        initialize_params.presetGUID, initialize_params.tuningInfo,
        &preset_config);
    if (nv_status != 0) {
        printf("nvEncGetEncodePresetConfigEx: %d\n", nv_status);
        return 1;
    }
    initialize_params.encodeConfig = &preset_config.presetCfg;
    // initialize_params.encodeConfig->frameIntervalP = 3;
    // initialize_params.encodeConfig->gopLength = 50;
    printf("frameIntervalP: %d, gopLength: %d\n",
           initialize_params.encodeConfig->frameIntervalP,
           initialize_params.encodeConfig->gopLength);

    nv_status =
        self->fns.nvEncInitializeEncoder(self->encoder, &initialize_params);
    if (nv_status != 0) {
        printf("nvEncInitializeEncoder: %d\n", nv_status);
        return 1;
    }

    uint32_t input_formats_count;
    nv_status = self->fns.nvEncGetInputFormatCount(
        self->encoder, initialize_params.encodeGUID, &input_formats_count);
    if (nv_status != 0) {
        return 1;
    }

    NV_ENC_BUFFER_FORMAT* input_formats =
        malloc(input_formats_count * sizeof(NV_ENC_BUFFER_FORMAT));
    nv_status = self->fns.nvEncGetInputFormats(
        self->encoder, initialize_params.encodeGUID, input_formats,
        input_formats_count, &input_formats_count);
    if (nv_status != 0) {
        free(input_formats);
        return 1;
    }

    *out_selected_format = NV_ENC_BUFFER_FORMAT_UNDEFINED;
    for (int i = 0; i < wanted_formats_len &&
                    *out_selected_format == NV_ENC_BUFFER_FORMAT_UNDEFINED;
         i++) {
        for (int j = 0; j < input_formats_count; j++) {
            NV_ENC_BUFFER_FORMAT a, b;
            a = wanted_formats[i];
            b = input_formats[j];
            if (a == b) {
                *out_selected_format = a;
                break;
            }
        }
    }
    free(input_formats);

    if (*out_selected_format == NV_ENC_BUFFER_FORMAT_UNDEFINED) {
        printf("none of wanted formats matched\n");
        return 1;
    }
    self->buffer_format = *out_selected_format;

    NV_ENC_CREATE_INPUT_BUFFER create_input_buffer_params = {0};
    create_input_buffer_params.version = NV_ENC_CREATE_INPUT_BUFFER_VER;
    create_input_buffer_params.bufferFmt = *out_selected_format;
    create_input_buffer_params.height = self->height;
    create_input_buffer_params.width = self->width;
    nv_status = self->fns.nvEncCreateInputBuffer(self->encoder,
                                                 &create_input_buffer_params);
    if (nv_status != 0) {
        return 1;
    }
    self->input_buffer = create_input_buffer_params.inputBuffer;

    NV_ENC_CREATE_BITSTREAM_BUFFER create_bitstream_buffer_params = {0};
    create_bitstream_buffer_params.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
    nv_status = self->fns.nvEncCreateBitstreamBuffer(
        self->encoder, &create_bitstream_buffer_params);
    if (nv_status != 0) {
        return 1;
    }
    self->bitstream_buffer = create_bitstream_buffer_params.bitstreamBuffer;

    return 0;
}

int NvEncodeSession_new(NvEncodeSession* self, uint32_t width, uint32_t height,
                        uint32_t framerate_num, uint32_t framerate_den) {
    NvEncodeSession _nv_encode_session_zero = {0};
    *self = _nv_encode_session_zero;

    self->width = width;
    self->height = height;
    self->framerate_num = framerate_num;
    self->framerate_den = framerate_den;

    NVENCSTATUS nv_status;

    NV_ENCODE_API_FUNCTION_LIST fns = {
        .version = NV_ENCODE_API_FUNCTION_LIST_VER,
        .reserved = 0,
        .reserved2 = NULL,
    };
    nv_status = NvEncodeAPICreateInstance(&fns);
    if (nv_status != 0) {
        return 1;
    }
    self->fns = fns;

    CUcontext cu_context;
    CUresult cu_result;
    cu_result = cuInit(0);
    if (cu_result != 0) {
        printf("failed cuInit: %d\n", cu_result);
        return 1;
    }
    cu_result = cuCtxCreate(&cu_context, NULL, 0, 0);
    if (cu_result != 0) {
        printf("failed creating cuda context: %d\n", cu_result);
        return 1;
    }
    self->cu_context = cu_context;

    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params = {
        .version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER,
        .deviceType = NV_ENC_DEVICE_TYPE_CUDA,
        .device = cu_context,
        .reserved = 0,
        .apiVersion = NVENCAPI_VERSION,
        .reserved1 = 0,
        .reserved2 = NULL,
    };
    void* encoder;
    nv_status = fns.nvEncOpenEncodeSessionEx(&params, &encoder);
    if (nv_status != 0) {
        return 1;
    }
    self->encoder = encoder;

    return 0;
}

int main() {
    NvEncodeSession nv_encode_session;
    NVENCSTATUS nv_status;
    nv_status = NvEncodeSession_new(&nv_encode_session, 1920, 1080, 30, 1);
    if (nv_status != 0) {
        NvEncodeSession_print_last_error(&nv_encode_session);
        return 1;
    }

    NV_ENC_BUFFER_FORMAT buffer_format = NV_ENC_BUFFER_FORMAT_ARGB;
    NV_ENC_BUFFER_FORMAT out_buffer_format;
    nv_status = NvEncodeSession_init_h264(&nv_encode_session, &buffer_format, 1,
                                          &out_buffer_format);
    if (nv_status != 0) {
        printf("failed to init h264\n");
        return 1;
    }

    EncodedFrame encoded_frame;
    uint32_t input_len = 1920 * 1080 * 4;
    void* input_data = malloc(input_len);
    memset(input_data, 0, input_len);
    nv_status = NvEncodeSession_encode_frame(&nv_encode_session, input_data,
                                             &encoded_frame);
    if (nv_status != 0) {
        printf("failed to encode frame\n");
        return 1;
    }

    printf("encoded_frame_len: %d\n", encoded_frame.len);

    fwrite(encoded_frame.data, 1, encoded_frame.len, stderr);

    if (NvEncodeSession_destroy(nv_encode_session) != 0) {
        printf("failed to destroy\n");
        return 1;
    }
    EncodedFrame_destroy(encoded_frame);

    return 0;
}
