#include "handler.hpp"

#include <cstring>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <limits>

#include <signal.h>
#include <unistd.h>

namespace console {

    const char *_ERROR = "\033[31;1m";
    const char *_HELP = "\033[32;1m";
    const char *_DEFAULT = "\033[0m";
    const char *_BOLD = "\033[1m";
    const char *_INFO = "\033[34;1m";

    // @formatter:off
    const char *USAGE = "SIGSEGV handler + regdump v.1.0.0\n"
                        "Usage: ./segfault (no arguments)";
    // @formatter:on

    class async_signal_safe_printer {

        unsigned int _set_length;

    public:

        async_signal_safe_printer() = default;

        async_signal_safe_printer(async_signal_safe_printer const &other) : _set_length(other._set_length) {};

        async_signal_safe_printer &operator<<(char flag) {
            _set_length = static_cast<unsigned int>(flag > 'a' ? 10 + flag - 'a' : flag - '0');
            return *this;
        }

        async_signal_safe_printer &operator<<(char const *message) {
            if (_set_length == 0) {
                ::write(1, message, strlen(message));
            } else {
                char buf[_set_length + 1];
                unsigned int n = static_cast<unsigned int>(strlen(message));
                for (unsigned int i = 0; i < _set_length; i++) {
                    buf[i] = i < n ? message[i] : ' ';
                }
                ::write(1, buf, _set_length);
            }
            return *this;
        }

        async_signal_safe_printer &operator<<(size_t hex) {
            char buf[_set_length + 1];
            for (unsigned int i = 0; i < _set_length; i++) {
                unsigned int mod = static_cast<unsigned int>(hex % 16);
                buf[_set_length - i - 1] = static_cast<char>(mod > 9 ? 'a' + (mod - 10) : '0' + mod);
                hex /= 16;
            }
            ::write(1, buf, _set_length);
            return *this;
        }

    };

    async_signal_safe_printer out() {
        return async_signal_safe_printer();
    }

    int report(char const *message, int err = 0) {
        out() << _ERROR << message;
        if (err != 0) {
            out() << strerror(errno);
        }
        out() << "\n" << _DEFAULT;
        return 0;
    }

    void notify(char const *message) {
        out() << _INFO << message << "\n" << _DEFAULT;
    }

}

jmp_buf handler::jmp;

handler::handler() {
    struct sigaction action{};

    action.sa_flags = SA_NODEFER | SA_SIGINFO;
    action.sa_sigaction = &handle;

    if (sigaction(SIGSEGV, &action, nullptr) < 0) {
        throw handler_exception("Could not set signal handler", errno);
    }
}

void handler::cause_segfault() {
    const char *test = "Hello, world, de-gozaru!";
    const_cast<char *>(test)[0] = 'X';

    *(int *) 0 = 0;

    raise(SIGSEGV);

    void *data = malloc(1);
    reinterpret_cast<char *>(data)[2] += 4;
}

void handler::dump_registers(ucontext_t *ucontext) {
    static std::vector<std::pair<const char *, int>> registers{
            {"R8",      REG_R8},
            {"R9",      REG_R9},
            {"R10",     REG_R10},
            {"R11",     REG_R11},
            {"R12",     REG_R12},
            {"R13",     REG_R13},
            {"R14",     REG_R14},
            {"R15",     REG_R15},
            {"RAX",     REG_RAX},
            {"RBP",     REG_RBP},
            {"RBX",     REG_RBX},
            {"RCX",     REG_RCX},
            {"RDI",     REG_RDI},
            {"RDX",     REG_RDX},
            {"RIP",     REG_RIP},
            {"RSI",     REG_RSI},
            {"RSP",     REG_RSP},
            {"CR2",     REG_CR2},
            {"CSGSFS",  REG_CSGSFS},
            {"EFL",     REG_EFL},
            {"ERR",     REG_ERR},
            {"OLDMASK", REG_OLDMASK},
            {"TRAPNO",  REG_TRAPNO}
    };

    console::notify("REGISTERS");
    for (auto const &pair : registers) {
        console::out() << '8' << pair.first << ": " << 'g' <<
                       static_cast<size_t>(ucontext->uc_mcontext.gregs[pair.second]) << '0' << "\n";
    }
}

void handler::dump_memory(void *address) {
    char *mem = reinterpret_cast<char *>(address);

    struct sigaction action{};

    action.sa_flags = SA_NODEFER | SA_SIGINFO;
    action.sa_sigaction = &handle_inner;

    if (sigaction(SIGSEGV, &action, nullptr) < 0) {
        throw handler_exception("Could not set signal handler to inner handler", errno);
    }

    char *from = mem > reinterpret_cast<char *>(32) ? mem - 32 : nullptr;
    char *to = mem < std::numeric_limits<char *>::max() - 32 ? mem + 32 : std::numeric_limits<char *>::max();
    console::notify("MEMORY");

    console::out() << console::_ERROR << "FROM " << 'g' << reinterpret_cast<size_t>(from) << '0' << " TO "
                   << 'g' << reinterpret_cast<size_t>(to) << '0' << console::_DEFAULT << "\n";

    for (char *cell = from; cell <= to; cell++) {
        if (cell == mem) {
            console::out() << "[-> ";
        }
        if (setjmp(jmp) < 0) {
            console::out() << "-- ";
        } else {
            console::out() << '2' << static_cast<size_t>(*cell & 0xFFu) << '0' << " ";
        }
        if (cell == mem) {
            console::out() << "<-] ";
        }
    }
    console::out() << "\n";
}

void handler::handle(int signal, siginfo_t *siginfo, void *context) {
    if (siginfo->si_signo == SIGSEGV) {
        dump_registers(reinterpret_cast<ucontext_t *>(context));
        dump_memory(siginfo->si_addr);
        exit(-1);
    }
}

void handler::handle_inner(int, siginfo_t *, void *) {
    longjmp(jmp, -1);
}

int main() {

    handler().cause_segfault();

}