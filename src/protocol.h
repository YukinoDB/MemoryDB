#ifndef YUKINO_PROTOCOL_H_
#define YUKINO_PROTOCOL_H_

#ifdef __cplusplus
namespace yukino {
#endif

enum CmdCode {
    CMD_AUTH,
    CMD_SELECT,

    // table command:
    CMD_GET,
    CMD_KEYS,
    CMD_SET,
    CMD_DELETE,

    
};

// ERROR
// STRING
// INTEGER
// ARRAY
//
// the object struct:
// ARRAY 2
//   ARRAY 2
//     STRING  name
//     STRING  jake
//   ARRAY 2
//     STRING  id
//     INTEGER 100
//
enum BinType {
    TYPE_NIL,     // 0
    TYPE_ERROR,   // 1
    TYPE_ARRAY,   // 2
    TYPE_STRING,  // 3
    TYPE_INTEGER, // 4
};


#ifdef __cplusplus
} // namespace yukino
#endif

#ifdef __cplusplus
struct command {
    const char *z;
    yukino::CmdCode code;
    int argc;
};

extern "C"
const struct command *yukino_command (const char *str, unsigned int len);

#endif // __cplusplus

#endif // YUKINO_PROTOCOL_H_