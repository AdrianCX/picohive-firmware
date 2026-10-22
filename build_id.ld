SECTIONS
{
    .note.gnu.build-id : {
          __build_id_start = .;
          KEEP(*(.note.gnu.build-id))
          __build_id_end = .;
    }
} INSERT AFTER .text;

ASSERT(DEFINED(__pico_os_slot_size) ? (__flash_binary_end - 0x10000000 <= __pico_os_slot_size) : 1, "firmware is larger than its flash slot, see os/config/flash_layout.h")
