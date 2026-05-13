#define LIBHWENCDEC_DECODER_IMPLEMENTATION
#define LIBHWENCDEC_ENCODER_IMPLEMENTATION

#include <stdio.h>

#include "decode.h"
#include "encode.h"

int main() {
    cuInit(0);

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

    NvDecodeSession nv_decode_session;
    if (NvDecodeSession_new_h264(&nv_decode_session, 1920, 1080) != 0) {
        printf("nvdecodesssion_new_h264\n");
        return 1;
    }

    if (NvDecodeSession_parse_video_data(&nv_decode_session, encoded_frame.data, encoded_frame.len) != 0) {
        printf("parse_video_data failed\n");
        return 1;
    }

    printf("EXITED!!!\n");
    return 0;
}
