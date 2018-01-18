#include <vga>
#include <arch.hpp>
#include <ringbuffer>
#include <hw/ps2.hpp>

static const int SUGGEST_READ_SIZE = 800;

static RingBuffer emerg_buffer(1 << 17);
static bool procedure_entered = false;
static const char* read_position = nullptr;
static const char* read_minpos = nullptr;
static const char* read_maxpos = nullptr;
static size_t read_size = 0;

static void emergency_logging(const char* data, int length)
{
  int free = emerg_buffer.free_space();
  if (free < length) {
    emerg_buffer.discard(length - free);
  }
  emerg_buffer.write(data, length);
}

static inline
const char* find_rev_nth(const char* ptr, const char* begin, int counter)
{
  while (ptr > begin && counter > 0)
  {
    if (*ptr-- == '\n') counter--;
  }
  return ptr;
}
static inline
const char* find_nth(const char* ptr, const char* end, int counter)
{
  while (ptr < end && counter > 0)
  {
    if (*ptr++ == '\n') counter--;
  }
  return ptr;
}


static void render_vga_text()
{
  auto& vga = TextmodeVGA::get();
  vga.clear();
  auto* read_to = find_rev_nth(read_position, emerg_buffer.data(), 25);
  vga.write(read_to, read_position - read_to);
}

extern "C" void (*current_eoi_mechanism)();
extern "C" void (*current_intr_handler)();
extern "C"
void keyboard_emergency_handler()
{
  render_vga_text();
  using namespace hw;
  int key = KBM::get_kbd_vkey();
  switch (key)
  {
  case KBM::VK_UP:
      read_position = find_rev_nth(read_position, read_minpos, 2);
      break;
  case KBM::VK_DOWN:
      read_position = find_nth(read_position, read_maxpos, 2);
      break;
  }
  // update
  current_eoi_mechanism();
}

extern "C"
void panic_perform_inspection_procedure()
{
  if (procedure_entered) return;
  procedure_entered = true;

  const char* EMERG_INFO =
      "\n>>> VGA EMERGENCY MODE ENTERED! <<<\n"
      "Use arrow keys to navigate logbuffer\n";

  emerg_buffer.write(EMERG_INFO, strlen(EMERG_INFO));

  // disable all IRQs
  for (uint8_t i = 0; i < 128; i++)
    __arch_unsubscribe_irq(i);

  // enable interrupts again
  asm("sti");

  // enable keyboard and mouse
  hw::KBM::init();

  // make buffer sequential
  auto* buffer = emerg_buffer.sequentialize();

  // set position and size
  read_size = std::min(emerg_buffer.size(), SUGGEST_READ_SIZE);
  read_maxpos = buffer + emerg_buffer.size() - 1;
  read_minpos = find_nth(buffer, read_maxpos, 21);

  read_position = read_maxpos;
  render_vga_text();

  const uint8_t kbd_irq = hw::KBM::get_kbd_irq();

  __arch_subscribe_irq(kbd_irq);
  current_intr_handler = keyboard_emergency_handler;
}

static __attribute__((constructor))
void hest()
{
  OS::add_stdout(emergency_logging);
}
