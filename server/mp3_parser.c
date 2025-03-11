#include "mp3_parser.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <errno.h>
/*
 * 跳过 MP3 文件开头可能存在的 ID3v2 标签
 * ID3v2 标签头的 10 字节中，第 6～9 字节存储标签大小（syncsafe 编码）
 */
static long skip_id3v2(FILE *fp)
{
    char header[10];
    if (fread(header, 1, 10, fp) != 10)
        return 0;

    if (memcmp(header, "ID3", 3) == 0)
    {
        long tag_size = ((header[6] & 0x7F) << 21) |
                        ((header[7] & 0x7F) << 14) |
                        ((header[8] & 0x7F) << 7) |
                        (header[9] & 0x7F);
        // 跳过整个标签区域：标签头 10 字节 + tag_size
        fseek(fp, 10 + tag_size, SEEK_SET);
        return 10 + tag_size;
    }
    fseek(fp, 0, SEEK_SET);
    return 0;
}

/*
 * 根据比特率索引获取 MPEG-1 Layer III 的比特率（单位 kbit/s）
 * 注意：索引 0 为 free、15 为 bad，这里简单返回 0
 */
static int get_bitrate_from_index(int bitrate_index)
{
    int bitrate_table[16] = {0, 32, 40, 48, 56, 64, 80, 96,
                             112, 128, 160, 192, 224, 256, 320, 0};
    if (bitrate_index < 0 || bitrate_index > 15)
        return 0;
    return bitrate_table[bitrate_index];
}

/*
 * 解析 MP3 文件的第一个有效帧头，返回 MPEG-1 Layer III 的比特率（kbit/s）
 * 出错时返回 -1
 */
int get_mp3_bitrate(const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp)
    {
        syslog(LOG_ERR, "fopen():%s", strerror(errno));
        return -1;
    }

    // 跳过可能存在的 ID3v2 标签
    skip_id3v2(fp);

    uint32_t header;
    // 滑动窗口查找第一个符合同步位条件的帧头
    while (fread(&header, 1, 4, fp) == 4)
    {
        header = ntohl(header);
        if ((header & 0xffe00000) == 0xffe00000)
            break;
        // 回退 3 字节，形成滑动窗口
        fseek(fp, -3, SEEK_CUR);
    }
    if (feof(fp))
    {
        fclose(fp);
        return -1;
    }

    // 解析帧头字段
    int version = (header >> 19) & 0x3;       // MPEG Audio version ID，2 位
    int layer = (header >> 17) & 0x3;         // Layer 描述，2 位
    int bitrate_index = (header >> 12) & 0xF; // 比特率索引，4 位

    // 本示例仅处理 MPEG-1 Layer III 格式（version==3 && layer==1）
    if (version != 3 || layer != 1)
    {
        fclose(fp);
        return -1;
    }

    int bitrate = get_bitrate_from_index(bitrate_index);
    fclose(fp);
    return bitrate;
}
