// Vita only: pads the end of the text segment.
//
// vita-elf-create appends ~4.6 KB of SCE module data right after the end of segment 0.
// The vita linker script starts the data segment (segment 1) at the next 64 KB boundary
// after the text segment, so when the text segment happens to end within ~4.6 KB below such a
// boundary the conversion fails with:
//   "Cannot allocate 4624 bytes for SCE data at end of segment 0; segment 1 overlaps"
// Changing the size of the text segment moves its end; SRR2_VITA_TEXT_PAD (CMake cache variable)
// is the size of this array in bytes.
#ifdef RAD_VITA

#ifndef SRR2_VITA_TEXT_PAD
#define SRR2_VITA_TEXT_PAD 8192
#endif

extern "C" {
// Non-zero initializer and const: stays in .rodata (inside the text segment), not in .bss.
__attribute__((used, aligned(16)))
const unsigned char srr2_vita_text_pad[SRR2_VITA_TEXT_PAD] = { 1 };
}

#endif