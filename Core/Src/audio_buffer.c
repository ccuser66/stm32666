#include "audio_buffer.h"

/* 音频缓冲结构体 */
typedef struct {
    int16_t buffer[AUDIO_BUFFER_SIZE];
    uint16_t write_idx;
    bool buffer_ready;
} AudioBuffer;

static AudioBuffer audio_buf = {0};

/* 初始化音频缓冲 */
void AudioBuffer_Init(void)
{
    audio_buf.write_idx = 0;
    audio_buf.buffer_ready = false;
}

/* 从蓝牙数据加载音频样本 */
void AudioBuffer_LoadFromBT(uint8_t* bt_data, uint16_t len)
{
    /* 将BT接收的数据转换为int16_t */
    uint16_t sample_count = len / 2;  /* 每个样本2字节 */
    
    for(uint16_t i = 0; i < sample_count; i++){
        /* 小端字节序转换 */
        int16_t sample = (int16_t)((bt_data[i*2+1] << 8) | bt_data[i*2]);
        
        audio_buf.buffer[audio_buf.write_idx++] = sample;
        
        if(audio_buf.write_idx >= AUDIO_BUFFER_SIZE){
            audio_buf.write_idx = 0;
            audio_buf.buffer_ready = true;  /* 缓冲满，准备处理 */
            break;
        }
    }
}

/* 检查缓冲是否准备好处理 */
bool AudioBuffer_IsReady(void)
{
    return audio_buf.buffer_ready;
}

/* 获取音频数据指针 */
int16_t* AudioBuffer_GetData(void)
{
    return audio_buf.buffer;
}

/* 重置缓冲 */
void AudioBuffer_Reset(void)
{
    audio_buf.buffer_ready = false;
    audio_buf.write_idx = 0;
}
