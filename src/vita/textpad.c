/* Chess Online: text-segment pad. vita-elf-create needs ~3 KB free after the text segment for its own data; when
 * the text happens to end just below a 64 KB boundary it reports "segment 1 overlaps". scripts/chessonline/build.sh
 * then measures the gap and rebuilds with CO_TEXT_PAD bytes here, which moves the end past the boundary. */
#ifndef CO_TEXT_PAD
#define CO_TEXT_PAD 1
#endif

const unsigned char co_text_pad[CO_TEXT_PAD] __attribute__((used)) = {1};

unsigned co_text_pad_size(void)
{
    return (unsigned)sizeof co_text_pad + co_text_pad[0] - 1u;
}
