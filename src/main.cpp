//
// Created by Alex Abdugafarov on 2023-12-17.
//

#include <iostream>
#include <string>
#include <fstream>
#include <codecvt>

//
// Structs
//

struct IdPair {
  uint32_t id1;
  uint32_t id2;
};

struct Email {
  std::string name;
  // Point to file bytes
  IdPair* id_pair;
};

// Matches data layout within mra.dbs
struct MessageHeader {
  uint32_t size;
  uint32_t prev_id;
  uint32_t next_id;
  uint32_t _unknown1;
  // WinApi FILETIME
  uint64_t time;
  uint32_t type;
  uint8_t flag_incoming;
  uint8_t _unknown2[3];
  uint32_t nickname_length;
  uint32_t magic_number;
  // In UTF-16 characters, not bytes
  uint32_t message_length;
  uint32_t _unknown3;
  // Byte
  uint32_t size_lps_rtf;
  uint32_t _unknown4;
};

struct Message {
  // Point to file bytes
  MessageHeader* header;
  std::string author;
  std::string text;
};

//
// Consts and functions
//

// "mrahistory_" UTF-16 (LE) bytes
const uint8_t mrahistory[] = {
    0x6D, 0x00, 0x72, 0x00, 0x61, 0x00, 0x68, 0x00, 0x69, 0x00, 0x73, 0x00, 0x74, 0x00, 0x6F, 0x00,
    0x72, 0x00, 0x79, 0x00, 0x5F, 0x00
};

template<typename... Args>
void log(const char* pattern, Args... args) {
#ifndef NDEBUG
  if (printf(pattern, args...) < 0) {
    perror("printf");
    exit(1);
  }
#endif
}

void assert(bool cond) {
  if (!cond) {
    printf("Assertion failed!\n");
    exit(1);
  }
}

std::vector<uint8_t> read_file(std::string& path) {
  // Open the file:
  std::ifstream file(path, std::ios::binary);

  // Stop eating new lines in binary mode!
  file.unsetf(std::ios::skipws);

  // Get its size
  file.seekg(0, std::ios::end);
  std::streampos file_size = file.tellg();
  file.seekg(0, std::ios::beg);

  // Reserve capacity
  std::vector<uint8_t> vec(file_size);

  // Read the data
  file.read((char*) &vec[0], file_size);

  return vec;
}

std::vector<Email> get_history(const uint8_t* file, size_t file_size, const uint32_t offset_table[]) {
  const int LAST_EMAIL_OFFSET = 0x2C;
  const int MRAHISTORY_OFFSET = 0x190;

  uint32_t last_email = *(uint32_t * )(file + offset_table[1] + LAST_EMAIL_OFFSET);

  log("last_email = 0x%08x, %i\n", last_email, last_email);

  std::vector<Email> emails;

  while (last_email) {
    uint32_t current_offset = offset_table[last_email];
    assert(current_offset < file_size);

    IdPair* id_pair = (struct IdPair*) (file + current_offset + 4);

    uint8_t* mrahistory_loc = (uint8_t*) id_pair + MRAHISTORY_OFFSET;
    if (memmem(mrahistory_loc, sizeof(mrahistory), mrahistory, sizeof(mrahistory))) {
      // Setting the pointer right after the "mrahistory_"
      auto name_utf16 = (char16_t*) (mrahistory_loc + sizeof(mrahistory));

      std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> utf16_to_utf8;
      Email email{
          .name=utf16_to_utf8.to_bytes(name_utf16),
          .id_pair=(IdPair*) ((uint8_t*) id_pair + 0x24),
      };

      log("mail_data at offset 0x%08x: Adding with name %s\n", current_offset, email.name.c_str());

      emails.push_back(email);
    } else {
      log("mail_data at offset 0x%08x: Skipping as it doesn't seem to be message related\n", current_offset);
    }
    last_email = id_pair->id2;
  }

  return emails;
}

std::vector<Message> get_messages(const uint8_t* file, size_t file_size, const uint32_t offset_table[], Email& email) {
  const int TYPE_SMS = 0x11;

  std::vector<Message> msgs;

  uint32_t msg_id = email.id_pair->id1;
  while (msg_id != 0) {
    MessageHeader* header = (MessageHeader*) (file + offset_table[msg_id]);
    assert(header->magic_number == 0x38);

    char16_t* author = (char16_t*) ((unsigned char*) header + sizeof(MessageHeader));

    char16_t* text = author + header->nickname_length;

    // TODO: This seem to never occur?
    if (*text == 0 && header->type == TYPE_SMS) {
      text += 3;
    }

    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> utf16_to_utf8;

    msgs.push_back(Message{
        .header = header,
        .author = utf16_to_utf8.to_bytes(author),
        .text = utf16_to_utf8.to_bytes(text),
    });

    msg_id = header->prev_id;
  };

  return msgs;
}

int main(int argc, char* argv[]) {
  if (argc != 2) {
    printf("Only one argument should be provided - mra.dbs path");
    exit(1);
  }

  std::string path(argv[1]);

  // Open the file:
  auto file = read_file(path);
  uint8_t* mra_base = &file[0];

  const int OFFSETS_TABLE_LOC_OFFSET = 0x10;

  auto offsets_table_offset = *(uint32_t * )(mra_base + OFFSETS_TABLE_LOC_OFFSET);
  log("Offset table is at offset 0x%08x\n", offsets_table_offset);
  auto* offsets_table = (uint32_t * )(mra_base + offsets_table_offset);

  size_t file_size = file.size();

  auto emails = get_history(&file[0], file_size, offsets_table);
  log("Found %i emails\n", emails.size());

  uint64_t msgs_count = 0;
  for (auto& email: emails) {
    log("=== %s\n", email.name.c_str());
    auto messages = get_messages(&file[0], file_size, offsets_table, email);
    msgs_count += messages.size();
    log("Parsed %i messages\n\n", messages.size());
    for (auto& msg: messages) {
      log("%s\n", msg.author.c_str());
      log("%s\n\n", msg.text.c_str());
    }
    log("%s\n", "===\n");
  }
  log("Found %i total messages", msgs_count);

  return 0;
}
