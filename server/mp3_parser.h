#ifndef MP3_PARSER_H
#define MP3_PARSER_H

/**
 * @brief 解析 MP3 文件，获取比特率（单位 kbit/s）。
 *
 * @param filename MP3 文件的路径
 * @return 比特率（kbit/s），出错返回 -1
 */
int get_mp3_bitrate(const char *filename);

#endif // MP3_PARSER_H
