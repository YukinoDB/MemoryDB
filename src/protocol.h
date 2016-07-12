#ifndef YUKINO_PROTOCOL_H_
#define YUKINO_PROTOCOL_H_

#ifdef __cplusplus
namespace yukino {
#endif

#define DECL_COMMANDS(_) \
    _(AUTH,   1) \
    _(SELECT, 1) \
    _(DUMP,   0) \
    _(GET,    1) \
    _(SET,    2) \
    _(DELETE, 1) \
    _(KEYS,   0) \
    _(LIST,   0) \
    _(LLEN,   1) \
    _(LPUSH,  2) \
    _(LPOP,   1) \
    _(RPUSH,  2) \
    _(RPOP,   1)

enum CmdCode {
#define DEF_CMD_CODE(name, argc) CMD_##name,
    DECL_COMMANDS(DEF_CMD_CODE)
#undef DEF_CMD_CODE
    MAX_COMMANDS,
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

namespace yukino {
    typedef command Command;

    extern const Command kCommands[];
} // namespace yukino

#endif // __cplusplus

#endif // YUKINO_PROTOCOL_H_