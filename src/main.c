#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include "common.h"
#include "interpreter.h"
#include "graphics.h"
#include "program.h"

static int quiet_mode = 0;
static int exit_on_finish = 0;

/* print_usage - Display command-line usage and available options */
static void print_usage(const char *prog) {
    printf("Usage: %s [options] [program.bas]\n", prog);
    printf("Options:\n");
    printf("  -w, --window     enable graphics window\n");
    printf("  --headless       enable headless graphics mode (virtual framebuffer without window)\n");
    printf("  -q, --quiet      suppress startup header and REPL prompts\n");
    printf("  -x, --exit-on-finish exit immediately after program finishes (only with -w)\n");
    printf("  -v, --version    show version information\n");
    printf("  -h, --help       show this help message\n");
}

/* handle_sigint - SIGINT (Ctrl+C) signal handler; sets the stop flag to
 * interrupt a running BASIC program gracefully */
void handle_sigint(int sig) {
    (void)sig;
    stop_running = 1;
}

/* repl - Read-Eval-Print Loop for interactive mode. Displays the startup
 * header and "Ok" prompt, reads lines from either the graphics window or
 * stdin, and feeds each line to the interpreter */
void repl() {
    char buffer[256];
    int active = graphics_is_active();
    char header[128];
    snprintf(header, sizeof(header), "IBM BASIKA Clone (DOS 2.1) v%s\nREADY.\n", BASIKA_VERSION);
    if (!quiet_mode) {
        if (active) graphics_print(header);
        else printf("%s", header);
    }

    while (1) {
        stop_running = 0; // Reset flag for new input
        if (graphics_is_active()) {
            if (!quiet_mode) graphics_print("Ok\n> ");
            graphics_readline(buffer, sizeof(buffer));
        } else {
            handle_events();
            if (!quiet_mode) printf("Ok\n> ");
            if (!fgets(buffer, sizeof(buffer), stdin)) break;
        }
        
        if (stop_running) continue;

        buffer[strcspn(buffer, "\n")] = 0;
        interpret_line(buffer, 1, NULL, 0);
    }
}

/* run_file - Load a BASIC program from the given file, store all numbered
 * lines, then execute with run_program(). Skips shebang lines so .bas files
 * can be made directly executable. Waits for a keypress before closing the
 * graphics window unless exit-on-finish mode is enabled */
void run_file(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("Error opening file");
        return;
    }
    char buffer[256];
    int first_line = 1;
    int last_line_num = 0;
    int source_line_number = 0;
    while (fgets(buffer, sizeof(buffer), f)) {
        source_line_number++;
        buffer[strcspn(buffer, "\n")] = 0;
        // Skip shebang line so .bas files can be marked executable
        if (first_line) {
            first_line = 0;
            if (buffer[0] == '#' && buffer[1] == '!') continue;
        }
        interpret_line(buffer, 1, &last_line_num, source_line_number);
    }
    fclose(f);
    run_program();

    if (graphics_is_active()) {
        if (!exit_on_finish) set_window_title("BASIKA Virtual Framebuffer - Press any key to quit...");
        graphics_present_now();
        if (!exit_on_finish) wait_for_keypress();
    }
}

/* main - Entry point. Parses command-line arguments, optionally initializes
 * the graphics subsystem, then either runs a .bas file or enters the
 * interactive REPL */
int main(int argc, char **argv) {
    int use_window = 0;
    int headless = 0;
    const char *filename = NULL;

    // Handle Ctrl+C in terminal
    signal(SIGINT, handle_sigint);
    srand(time(NULL));

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--window") == 0) {
            use_window = 1;
        } else if (strcmp(argv[i], "--headless") == 0) {
            use_window = 1;
            headless = 1;
            exit_on_finish = 1;
        } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
            quiet_mode = 1;
        } else if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--exit-on-finish") == 0) {
            exit_on_finish = 1;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("BASIKA %s\n", BASIKA_VERSION);
            return 0;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (filename == NULL) {
            filename = argv[i];
            extern void set_args(int argc, char **argv);
            set_args(argc - i, &argv[i]);
            break; // Remaining arguments are passed to the script
        }
    }

    if (use_window) {
        if (headless) {
            set_graphics_headless(1);
        }
        if (!init_graphics()) {
            fprintf(stderr, "Failed to initialize graphics window\n");
            return 1;
        }
    }

    if (filename) {
        run_file(filename);
    } else {
        repl();
    }
    return 0;
}
