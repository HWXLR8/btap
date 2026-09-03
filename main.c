#include <fcntl.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>

void read_exact(int fd, int len, unsigned char* buf) {
  int got = 0;
  while (got != len) {
    int r = read(fd, buf, len - got);
    got += r;
  }
}

// populates buf with the payload, and prints its contents
int read_frame(int fd, unsigned char* buf) {
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
  unsigned char TFI = buf[0];
  unsigned char data[LEN-1];
  memcpy(data, buf+1, LEN-1);
  unsigned char DCS = buf[LEN];
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

  default:
    printf("Unknown response code %02X\n", buf[0]);
  }
  printf("\n");
}

int main() {
  int fd = open("/dev/ttyUSB0", O_RDWR | O_NOCTTY);
  if (fd < 0) {
    perror("open");
    return 1;
  }

  struct termios t;
  tcgetattr(fd, &t);
  cfmakeraw(&t);
  cfsetspeed(&t, B115200);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 1;
  tcflush(fd, TCIFLUSH);

  // wake up
  unsigned char wake[16] = {0x55};
  write(fd, wake, sizeof wake);
  tcdrain(fd);

  /* unsigned char tfi_data[] = { */
  /*   0xD4, // TFI: host -> pn532 */
  /*   0x14, // opcode: SAMConfiguration */
  /*   0x01, // mode: normal, no SAM */
  /*   0x14, // timeout, 50ms x 20 (ignored in normal mode) */
  /*   0x01  // IRQ pin enabled */
  /* }; */
  /* int len = sizeof tfi_data; */
  /* int lcs = (0x100 - len) & 0xFF; // length checksum */
  /* int sum = 0; */
  /* for (int i = 0; i < len; ++i) sum += tfi_data[i]; */
  /* int dcs = (0x100 - sum) & 0xFF; */
  /* printf("LEN=%02X LCS=%02X DCS=%02X\n", len, lcs, dcs); */

  unsigned char frame[] = {
    0x00, 0x00, 0xFF,              // preamble+start code
    0x05, 0xFB,                    // LEN, LCS
    0xD4, 0x14, 0x01, 0x14, 0x01,  // TFI + SAMConfiguration
    0x02,                          // DCS
    0x00                           // postamble
  };
  write(fd, frame, sizeof frame);
  tcdrain(fd);

  // expect:
  // 00 00 FF 00 FF 00 = ACK
  // 00 00 FF   preamble + start code
  // 02 FE      LEN, LCS
  // D5         TFI: pn532 -> host
  // 15         SAMConfiguration response (command 0x14 + 1 = 0x15)
  // 16         DCS
  // 00         postamble
  unsigned char buf[256];
  if (read_frame(fd, buf)) {
    return 1;
  }
  parse_payload(buf);

  // poll for card
  unsigned char poll[] = {
    0x00, 0x00, 0xFF,  // preamble + start code
    0x04, 0xFC,        // LEN, LCS
    0xD4,              // TFI: host -> pn532
    0x4A,              // opcode: InListPassiveTarget
    0x01,              // MaxTg: report at most 1 target
    0x00,              // BrTy: 106 kbps ISO14443-A
    0xE1,              // DCS
    0x00               // postamble
  };
  write(fd, poll, sizeof poll);
  tcdrain(fd);

  /* for (int j = 0; j < 6; ++j) printf("%02X ", buf[j]); */
  printf("\n");

  if (read_frame(fd, buf)) {
    return 1;
  }
  parse_payload(buf);

  return 0;
}
