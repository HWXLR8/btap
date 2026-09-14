#include <fcntl.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>

#define PN532_PATH "/dev/ttyUSB0"

void read_exact(int fd, int len, unsigned char* buf) {
  int got = 0;
  while (got != len) {
    int r = read(fd, buf + got, len - got);
    got += r;
  }
}

// populates buf with the payload, and prints its contents
int read_response(int fd, unsigned char* buf) {
  // read 6 byte preamble
  read_exact(fd, 6, buf);
  if (memcmp(buf, "\x00\x00\xFF\x00\xFF\x00", 6)) {
    printf("No ACK received, exiting.");
    return 1;
  }

 // read 3 byte start of frame
  read_exact(fd, 3, buf);
  if (memcmp(buf, "\x00\x00\xFF", 3)) {
    printf("No preamble found, exiting.");
    return 1;
  }

 // read LEN+LCS
  read_exact(fd, 2, buf);
  unsigned char LEN = buf[0];
  unsigned char LCS = buf[1];
  if (((LEN+LCS) & 0xFF) != 0) {
    printf("Malformed LEN/LCS, exiting.");
    return 1;
  }

  // read TFI+data+DCS
  read_exact(fd, LEN+1, buf);
  // unsigned char TFI = buf[0];
  unsigned char data[LEN-1];
  memcpy(data, buf+1, LEN-1);
  // unsigned char DCS = buf[LEN];
  unsigned char sum = 0;
  for (int i = 0; i <= LEN; ++i) sum += buf[i];
  if (sum != 0) {
    printf("Malformed data/DCS, exiting");
    return 1;
  }

  // print payload contents
  for (int i = 0; i < LEN-1; ++i) {
    printf("%02X ", data[i]);
  }
  printf("\n");

  // copy payload into input buffer
  memcpy(buf, data, LEN-1);

  // read postamble
  unsigned char discard;
  read_exact(fd, 1, &discard);

  return 0;
}

// generic payload parser for non-page card contents
void parse_payload(unsigned char* buf) {
  int c = 0;
  switch (buf[c++]) {
    case 0x15:
      printf("SAMConfiguration Success");
      break;

    case 0x4B:
      printf("Opcode: InListPassiveTarget\n");
      int nbtg = buf[c];
      printf("NbTg: %02X\n", buf[c++]);
      for (int i = 0; i < nbtg; ++i) {
        printf("Tg: %02X\n", buf[c++]);
        printf("    SENS_RES: %02X", buf[c++]);
        printf("%02X\n", buf[c++]);
        printf("    SEL_RES: %02X\n", buf[c++]);
        int idlen = buf[c];
        printf("    NFCIDLength: %02X\n", buf[c++]);
        printf("    NFCID1: ");
        for (int j = 0; j < idlen; ++j) {
          printf("%02X ", buf[c++]);
        }
      }
      break;

    case 0x41:
      printf("Opcode: InDataExchange");
      break;

    default:
      printf("Unknown response code %02X\n", buf[0]);
  }
  printf("\n\n");
}

int read_ndef_memory(unsigned char* mem, char* mra_path) {
  if (mem[0] != 0x03) {
    printf("Expected 0x03 at beginning of NDEF memory, got: %02X\n", mem[0]);
    return 1;
  }

  // int len = mem[1];
  int c = 2;
  int flags = mem[c++];
  int SR = !!(flags & 0x10);
  int IL = !!(flags & 0x08);
  int ME = !!(flags & 0x40);
  int TNF = flags & 0x07;
  printf("SR=%d IL=%d ME=%d TNF=%d\n", SR, IL, ME, TNF);
  // Only support IL = 0
  if (IL != 0) {
    printf("Unsupported IL.\n");
    return 1;
  }

  int TL = mem[c++];
  printf("TYPE LENGTH: %02X\n", TL);
  if (TL != 1) {
    printf("Unsupported type length.\n");
    return 1;
  }

  int PL = mem[c++];
  printf("PAYLOAD LENGTH: %02X\n", PL);
  printf("TYPE: %c\n", mem[c++]);

  int status = mem[c++];
  printf("STATUS BYTE: %02X\n", status);
  printf("\tENCODING: ");
  (status & 0x80) ? printf("UTF-16\n") : printf("UTF-8\n");
  int langlen = status & 0x3F;
  printf("\tLANGLEN: %02X\n", langlen);
  printf("\tLANGUAGE: %.*s\n", langlen, &mem[c]);
  c += langlen;

  int n = PL - 1 - langlen;
  printf("%.*s\n", n, &mem[c]);

  // store in mra_path
  memcpy(mra_path, &mem[c], n);
  mra_path[n] = '\0';

  return 0;
}

// return the size of the data area in bytes
int get_ntag_info(unsigned char* buf) {
  int c = 0;

  // return code
  int code = buf[c++];
  if (code == 0x41) {
    printf("InDataExchange: ");
  } else {
    printf("Unexpected return code: %02X\n", code);
    return -1;
  }

  // status
  int status = buf[c++];
  if (status == 0x00) {
    printf("STATUS OK!\n");
  } else {
    printf("Unexpected status: %02X\n", status);
    return -1;
  }

  // jump to page 3 byte 2
  c += 12 + 2;
  int size = buf[c];
  switch (size) {
    case 0x12:
      printf("IC: NTAG213\n");
      break;

    case 0x3E:
      printf("IC: NTAG215\n");
      break;

    case 0x6D:
      printf("IC: NTAG216\n");
      break;

    default:
      printf("IC: UNKNOWN\n");
      printf("CC SIZE: %02X\n", size);
  }

  int memory_size = size * 8;
  printf("NDEF MEMORY SIZE: %d B\n\n", memory_size);
  return memory_size;
}

void send_frame(int fd, unsigned char* payload, int len) {
  int lcs = (0x100 - len) & 0xFF;
  int sum = 0;
  for (int i = 0; i < len; ++i) sum += payload[i];
  int dcs = (0x100 - sum) & 0xFF;
  int c = 0;
  unsigned char frame[len + 7];
  frame[c++] = 0x00;
  frame[c++] = 0x00;
  frame[c++] = 0xFF;
  frame[c++] = len;
  frame[c++] = lcs;
  memcpy(frame+5, payload, len);
  c += len;
  frame[c++] = dcs;
  frame[c++] = 0x00;
  write(fd, frame, sizeof frame);
  tcdrain(fd);
}

int init(int* fd, unsigned char* buf, unsigned char* payload) {
  *fd = open(PN532_PATH, O_RDWR | O_NOCTTY);
  if (*fd < 0) {
    perror("open");
    return 1;
  }

  struct termios t;
  tcgetattr(*fd, &t);
  cfmakeraw(&t);
  cfsetspeed(&t, B115200);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 1;
  tcsetattr(*fd, TCSANOW, &t);
  tcflush(*fd, TCIFLUSH);

  // wake up
  unsigned char wake[16] = {0x55};
  write(*fd, wake, sizeof wake);
  tcdrain(*fd);

  // SAM config
  memcpy(payload, (unsigned char[]){0xD4, 0x14, 0x01, 0x14, 0x01}, 5);
  send_frame(*fd, payload, 5);
  if (read_response(*fd, buf)) return 1;
  parse_payload(buf);

  return 0;
}

int poll(int* fd, unsigned char* buf, unsigned char* payload, char* mra_path) {
  // InListPassiveTarget
  memcpy(payload, (unsigned char[]){0xD4, 0x4A, 0x01, 0x00}, 4);
  send_frame(*fd, payload, 4);
  if (read_response(*fd, buf)) return 1;
  parse_payload(buf);

  // get NTAG info
  memcpy(payload, (unsigned char[]){0xD4, 0x40, 0x01, 0x30, 0x00}, 5);
  send_frame(*fd, payload, 5);
  if (read_response(*fd, buf)) return 1;
  int memory_size = get_ntag_info(buf);


  // read NDEF memory, one page at a time
  unsigned char ndef_memory[1024];
  for (int i = 16; i < memory_size; i += 16) { // 4 pages per query
    memcpy(payload, (unsigned char[]){0xD4, 0x40, 0x01, 0x30, i/4}, 5);
    send_frame(*fd, payload, 5);
    if (read_response(*fd, buf)) return 1;

    // copy respones into ndef_memory
    int offset = i - 16;
    memcpy(ndef_memory + offset, buf + 2, 16);
  }

  if (read_ndef_memory(ndef_memory, mra_path)) return 1;

  return 0;
}

int launch_game(char* mra_path) {
  FILE *f = fopen("/dev/MiSTer_cmd", "w");
  if (!f) { perror("/dev/MiSTer_cmd"); return 1; }
  fprintf(f, "load_core %s\n", mra_path);
  fclose(f);
  return 0;
}

int main() {
  int fd;
  unsigned char buf[256];
  unsigned char payload[256];
  char mra_path[512];
  char prev_mra[512] = {0};

  init(&fd, buf, payload);

  for (;;) {
    poll(&fd, buf, payload, mra_path);
    if (strcmp(mra_path, prev_mra) !=0) {
      launch_game(mra_path);
      strcpy((char*)prev_mra, mra_path);
    }
    usleep(200000); // 200ms
  }

  return 0;
}
