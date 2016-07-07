#ifndef YUKINO_VALUE_TRAITS_H_
#define YUKINO_VALUE_TRAITS_H_

#include "yuki/strings.h"

namespace yukino {

template<class T>
struct ValueTraits {
    static bool Parse(yuki::SliceRef buf, T *) {
        return false;
    }

    static std::string ToString(const T &) {
        return "";
    }
};

template<>
struct ValueTraits<int> {
    static bool Parse(yuki::SliceRef buf, int *value) {
        // longest int number: -2147483648
        if (buf.Empty() || buf.Length() > sizeof("-2147483648") - 1) {
            return false;
        }
        int sign = 0;
        if (buf.Data()[0] == '-') {
            if (buf.Length() == 1) {
                return false;
            }
            sign = 1;
        }

        int n = 0;
        int pow = 1;
        for (long i = buf.Length() - 1; i >= sign; i--) {
            auto c = buf.Data()[i];

            if (c < '0' || c > '9') {
                return false;
            } else {
                n += ((c - '0') * pow);
                pow *= 10;
            }
        }
        *value = sign ? -n : n;
        return true;
    }

    static std::string ToString(const int &value) {
        return yuki::Strings::Format("%d", value);
    }
};

template<>
struct ValueTraits<long> {
    static bool Parse(yuki::SliceRef buf, long *value) {
        // longest int number: -2147483648
        if (buf.Empty() || buf.Length() > sizeof("-9223372036854775808") - 1) {
            return false;
        }
        int sign = 0;
        if (buf.Data()[0] == '-') {
            if (buf.Length() == 1) {
                return false;
            }
            sign = 1;
        }

        long n = 0;
        long pow = 1;
        for (long i = buf.Length() - 1; i >= sign; i--) {
            auto c = buf.Data()[i];

            if (c < '0' || c > '9') {
                return false;
            } else {
                n += ((c - '0') * pow);
                pow *= 10;
            }
        }
        *value = sign ? -n : n;
        return true;
    }

    static std::string ToString(const int &value) {
        return yuki::Strings::Format("%d", value);
    }
};

template<>
struct ValueTraits<std::string> {
    static bool Parse(yuki::SliceRef buf, std::string *value) {
        if (buf.Empty()) {
            return false;
        }
        if (buf.Compare(yuki::Slice("\"\"", 2)) == 0) {
            value->assign("");
        } else {
            value->assign(buf.Data(), buf.Length());
        }
        return true;
    }

    static std::string ToString(const std::string &value) {
        return value.empty() ? "\"\"" : value;
    }
};

template<>
struct ValueTraits<bool> {
    static bool Parse(yuki::SliceRef buf, bool *value) {
        if (buf.Compare(yuki::Slice("yes", 3)) == 0 ||
            buf.Compare(yuki::Slice("true", 4)) == 0 ||
            buf.Compare(yuki::Slice("on", 2)) == 0) {
            
            *value = true;
        } else if (buf.Compare(yuki::Slice("no", 2)) == 0 ||
                   buf.Compare(yuki::Slice("false", 5)) == 0 ||
                   buf.Compare(yuki::Slice("off", 3)) == 0) {
            
            *value = false;
        } else {
            return false;
        }
        return true;
    }
    
    static std::string ToString(const bool &value) {
        return value ? "yes" : "no";
    }
};

} // namespace yukino

#endif // YUKINO_VALUE_TRAITS_H_