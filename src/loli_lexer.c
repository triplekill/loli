#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stddef.h>

#include "loli_config.h"
#include "loli_lexer.h"
#include "loli_utf8.h"
#include "loli_alloc.h"

#define CC_G_ONE_OFFSET  0
#define CC_RIGHT_PARENTH 0
#define CC_COMMA         1
#define CC_LEFT_CURLY    2
#define CC_RIGHT_CURLY   3
#define CC_LEFT_BRACKET  4
#define CC_COLON         5
#define CC_TILDE         6
#define CC_G_ONE_LAST    6

#define CC_G_TWO_OFFSET  7
#define CC_CARET         7
#define CC_NOT           8
#define CC_PERCENT       9
#define CC_MULTIPLY      10
#define CC_DIVIDE        11
#define CC_G_TWO_LAST    11

#define CC_GREATER       12
#define CC_LESS          13
#define CC_PLUS          14
#define CC_MINUS         15
#define CC_WORD          16
#define CC_DOUBLE_QUOTE  17
#define CC_NUMBER        18
#define CC_LEFT_PARENTH  19
#define CC_RIGHT_BRACKET 20

#define CC_EQUAL         21
#define CC_NEWLINE       22
#define CC_SHARP         23
#define CC_DOT           24
#define CC_AT            25
#define CC_AMPERSAND     26
#define CC_VBAR          27
#define CC_QUESTION      28
#define CC_B             29
#define CC_DOLLAR        30
#define CC_SINGLE_QUOTE  31
#define CC_INVALID       32

static int read_line(loli_lex_state *);
static void close_entry(loli_lex_entry *);

static const char ident_table[256] =
{
      
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
  0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,
  0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const loli_token grp_two_table[] =
{
    tk_bitwise_xor, tk_not, tk_modulo, tk_multiply, tk_divide
};

static const loli_token grp_two_eq_table[] =
{
    tk_bitwise_xor_eq, tk_not_eq, tk_modulo_eq, tk_multiply_eq, tk_divide_eq,
};


loli_lex_state *loli_new_lex_state(loli_raiser *raiser)
{
    loli_lex_state *lexer = loli_malloc(sizeof(*lexer));

    char *ch_class;

    lexer->entry = NULL;
    lexer->raiser = raiser;
    lexer->input_buffer = loli_malloc(128 * sizeof(*lexer->input_buffer));
    lexer->label = loli_malloc(128 * sizeof(*lexer->label));
    lexer->ch_class = NULL;
    lexer->last_literal = NULL;
    lexer->last_integer = 0;
    ch_class = loli_malloc(256 * sizeof(*ch_class));

    lexer->input_pos = 0;
    lexer->input_size = 128;
    lexer->label_size = 128;
     
    lexer->line_num = 0;

     
    memset(ch_class, CC_INVALID, (256 * sizeof(char)));

    int i;
    for (i = 'a';i <= 'z';i++)
        ch_class[i] = CC_WORD;

    for (i = 'A';i <= 'Z';i++)
        ch_class[i] = CC_WORD;

    for (i = '0';i <= '9';i++)
        ch_class[i] = CC_NUMBER;

     
    for (i = 194;i <= 244;i++)
        ch_class[i] = CC_WORD;

    ch_class[(unsigned char)'b'] = CC_B;
    ch_class[(unsigned char)'_'] = CC_WORD;
    ch_class[(unsigned char)'('] = CC_LEFT_PARENTH;
    ch_class[(unsigned char)')'] = CC_RIGHT_PARENTH;
    ch_class[(unsigned char)'"'] = CC_DOUBLE_QUOTE;
    ch_class[(unsigned char)'\''] = CC_SINGLE_QUOTE;
    ch_class[(unsigned char)'@'] = CC_AT;
    ch_class[(unsigned char)'?'] = CC_QUESTION;
    ch_class[(unsigned char)'#'] = CC_SHARP;
    ch_class[(unsigned char)'='] = CC_EQUAL;
    ch_class[(unsigned char)'.'] = CC_DOT;
    ch_class[(unsigned char)','] = CC_COMMA;
    ch_class[(unsigned char)'+'] = CC_PLUS;
    ch_class[(unsigned char)'-'] = CC_MINUS;
    ch_class[(unsigned char)'{'] = CC_LEFT_CURLY;
    ch_class[(unsigned char)'}'] = CC_RIGHT_CURLY;
    ch_class[(unsigned char)'<'] = CC_LESS;
    ch_class[(unsigned char)'>'] = CC_GREATER;
    ch_class[(unsigned char)':'] = CC_COLON;
    ch_class[(unsigned char)'!'] = CC_NOT;
    ch_class[(unsigned char)'*'] = CC_MULTIPLY;
    ch_class[(unsigned char)'/'] = CC_DIVIDE;
    ch_class[(unsigned char)'&'] = CC_AMPERSAND;
    ch_class[(unsigned char)'%'] = CC_PERCENT;
    ch_class[(unsigned char)'|'] = CC_VBAR;
    ch_class[(unsigned char)'^'] = CC_CARET;
    ch_class[(unsigned char)'['] = CC_LEFT_BRACKET;
    ch_class[(unsigned char)']'] = CC_RIGHT_BRACKET;
    ch_class[(unsigned char)'$'] = CC_DOLLAR;
    ch_class[(unsigned char)'~'] = CC_TILDE;
    ch_class[(unsigned char)'\n'] = CC_NEWLINE;

     
    lexer->token = tk_invalid;
    lexer->ch_class = ch_class;
    return lexer;
}

void loli_rewind_lex_state(loli_lex_state *lexer)
{
    if (lexer->entry) {
        loli_lex_entry *entry_iter = lexer->entry;
        while (entry_iter->prev)
            entry_iter = entry_iter->prev;

        lexer->entry = entry_iter;

        while (entry_iter) {
            if (entry_iter->source != NULL) {
                close_entry(entry_iter);
                entry_iter->source = NULL;
            }

            loli_free(entry_iter->saved_input);
            entry_iter->saved_input = NULL;
            entry_iter = entry_iter->next;
        }
    }

    lexer->last_literal = NULL;
    lexer->last_integer = 0;
    lexer->input_pos = 0;
}

void loli_free_lex_state(loli_lex_state *lexer)
{
    if (lexer->entry) {
        loli_lex_entry *entry_iter = lexer->entry;
        while (entry_iter->prev)
            entry_iter = entry_iter->prev;

        loli_lex_entry *entry_next;
        while (entry_iter) {
            if (entry_iter->source != NULL)
                close_entry(entry_iter);

            entry_next = entry_iter->next;
            loli_free(entry_iter->saved_input);
            loli_free(entry_iter);
            entry_iter = entry_next;
        }
    }

    loli_free(lexer->input_buffer);
    loli_free(lexer->ch_class);
    loli_free(lexer->label);
    loli_free(lexer);
}

static loli_lex_entry *get_entry(loli_lex_state *lexer)
{
    loli_lex_entry *ret_entry = NULL;

    if (lexer->entry == NULL ||
        (lexer->entry->source != NULL && lexer->entry->next == NULL)) {
        ret_entry = loli_malloc(sizeof(*ret_entry));

        if (lexer->entry == NULL) {
            lexer->entry = ret_entry;
            ret_entry->prev = NULL;
        }
        else {
            lexer->entry->next = ret_entry;
            ret_entry->prev = lexer->entry;
        }

        ret_entry->source = NULL;
        ret_entry->extra = NULL;
        ret_entry->saved_input = NULL;
        ret_entry->saved_input_pos = 0;

        ret_entry->next = NULL;
        ret_entry->lexer = lexer;
    }
    else {
        if (lexer->entry->source == NULL)
            ret_entry = lexer->entry;
        else
            ret_entry = lexer->entry->next;
    }

    if (ret_entry->prev) {
        loli_lex_entry *prev_entry = ret_entry->prev;
        char *new_input;
         
        if (prev_entry->saved_input == NULL)
            new_input = loli_malloc(lexer->input_size * sizeof(*new_input));
        else if (prev_entry->saved_input_size < lexer->input_size)
            new_input = loli_realloc(prev_entry->saved_input,
                    lexer->input_size * sizeof(*new_input));
        else
            new_input = prev_entry->saved_input;

        strcpy(new_input, lexer->input_buffer);
        prev_entry->saved_input = new_input;
        prev_entry->saved_line_num = lexer->line_num;
        prev_entry->saved_input_pos = lexer->input_pos;
        prev_entry->saved_input_size = lexer->input_size;
        prev_entry->saved_token = lexer->token;
        prev_entry->saved_last_literal = lexer->last_literal;
        prev_entry->saved_last_integer = lexer->last_integer;

        lexer->line_num = 0;
    }

    lexer->input_pos = 0;
    lexer->entry = ret_entry;

    return ret_entry;
}

void loli_pop_lex_entry(loli_lex_state *lexer)
{
    loli_lex_entry *entry = lexer->entry;

    close_entry(entry);
    entry->source = NULL;

    if (entry->prev) {
        entry = entry->prev;

        strcpy(lexer->input_buffer, entry->saved_input);

        lexer->line_num = entry->saved_line_num;
        lexer->input_pos = entry->saved_input_pos;
         
        lexer->entry = entry;
        lexer->last_literal = entry->saved_last_literal;
        lexer->last_integer = entry->saved_last_integer;

        lexer->token = entry->saved_token;

         
        if (lexer->token == tk_word || lexer->token == tk_prop_word ||
            lexer->token == tk_keyword_arg) {
            int end, pos;
            end = pos = lexer->input_pos;

            do {
                char ch = lexer->input_buffer[pos - 1];
                if (ident_table[(unsigned int)ch] == 0)
                    break;
                pos--;
            } while (pos);

            strncpy(lexer->label, lexer->input_buffer + pos, end - pos);
            lexer->label[(end - pos)] = '\0';
        }
    }
    else
         
        lexer->line_num = 0;
}

#define READER_PREP \
int bufsize, i; \
loli_lex_state *lexer = entry->lexer; \
char *input_buffer = lexer->input_buffer; \
 \
bufsize = lexer->input_size; \
i = 0; \
int utf8_check = 0;

#define READER_GROW_CHECK \
if ((i + 2) == bufsize) { \
    loli_grow_lexer_buffers(lexer); \
    input_buffer = lexer->input_buffer; \
    bufsize = lexer->input_size; \
}

#define READER_EOF_CHECK(to_check, against) \
if (to_check == against) { \
    input_buffer[i] = '\n'; \
    input_buffer[i + 1] = '\0'; \
      \
    lexer->line_num += !!i; \
    break; \
}

#define READER_END \
if (utf8_check && loli_is_valid_utf8(input_buffer) == 0) { \
    loli_raise_err(lexer->raiser, "Invalid utf-8 sequence on line %d.", \
            lexer->line_num); \
} \
 \
return i;


static int read_file_line(loli_lex_entry *entry)
{
    READER_PREP
    FILE *input_file = (FILE *)entry->source;
    int ch;

    while (1) {
        ch = fgetc(input_file);

        READER_GROW_CHECK
        READER_EOF_CHECK(ch, EOF)

        input_buffer[i] = ch;

        if (ch == '\r' || ch == '\n') {
            lexer->line_num++;

            if (ch == '\r') {
                input_buffer[i] = '\n';
                ch = fgetc(input_file);
                if (ch != '\n')
                    ungetc(ch, input_file);
            }

            i++;
            input_buffer[i] = '\0';
            break;
        }
        else if ((unsigned char)ch > 127)
            utf8_check = 1;

        i++;
    }

    READER_END
}

static int read_str_line(loli_lex_entry *entry)
{
    READER_PREP
    char *ch = (char *)entry->source;

    while (1) {
        READER_GROW_CHECK
        READER_EOF_CHECK(*ch, '\0')

        input_buffer[i] = *ch;

        if (*ch == '\r' || *ch == '\n') {
            lexer->line_num++;

            if (*ch == '\r') {
                input_buffer[i] = '\n';
                ch++;
                if (*ch == '\n')
                    ch++;
            }
            else
                ch++;

            i++;
            input_buffer[i] = '\0';
            break;
        }
        else if (((unsigned char)*ch) > 127)
            utf8_check = 1;

        i++;
        ch++;
    }

    entry->source = ch;
    READER_END
}

#undef READER_PREP
#undef READER_GROW_CHECK
#undef READER_EOF_CHECK
#undef READER_END

static int read_line(loli_lex_state *lex)
{
    loli_lex_entry *entry = lex->entry;
    if (entry->entry_type == et_file)
        return read_file_line(entry);
    else
        return read_str_line(entry);
}

static void close_entry(loli_lex_entry *entry)
{
    if (entry->entry_type == et_file)
        fclose((FILE *)entry->source);
    else if (entry->entry_type != et_shallow_string)
         
        loli_free(entry->extra);
}


static char scan_escape(loli_lex_state *lexer, char **source_ch)
{
    char ret;
    char *ch = *source_ch;

    if (*ch == 'n')
        ret = '\n';
    else if (*ch == 'r')
        ret = '\r';
    else if (*ch == 't')
        ret = '\t';
    else if (*ch == '\'')
        ret = '\'';
    else if (*ch == '"')
        ret = '"';
    else if (*ch == '\\')
        ret = '\\';
    else if (*ch == 'b')
        ret = '\b';
    else if (*ch == 'a')
        ret = '\a';
    else if (*ch == '/')
        ret = LOLI_PATH_CHAR;
    else if (*ch >= '0' && *ch <= '9') {
         
        int i, value = 0, total = 0;
        for (i = 0;i < 3;i++, ch++) {
            if (*ch < '0' || *ch > '9')
                break;

            value = *ch - '0';
            if ((total * 10) + value > 255)
                break;

            total = (total * 10) + value;
        }

        ch -= 1;
        ret = (char)total;
    }
    else {
         
        ret = 0;
        loli_raise_syn(lexer->raiser, "Invalid escape sequence.");
    }

    *source_ch = ch + 1;

    return ret;
}


static void scan_exponent(loli_lex_state *lexer, int *pos, char *new_ch)
{
    int num_pos = *pos + 1;
    int num_digits = 0;

    new_ch++;
    if (*new_ch == '+' || *new_ch == '-') {
        num_pos++;
        new_ch++;
    }

    if (*new_ch < '0' || *new_ch > '9')
        loli_raise_syn(lexer->raiser,
                   "Expected a base 10 number after exponent.");

    while (*new_ch >= '0' && *new_ch <= '9') {
        num_digits++;
        if (num_digits > 3) {
            loli_raise_syn(lexer->raiser, "Exponent is too large.");
        }
        num_pos++;
        new_ch++;
    }

    *pos = num_pos;
}

static uint64_t scan_binary(int *pos, char *ch)
{
    uint64_t result = 0;
    int num_digits = 0;
    int max_digits = 65;
    int num_pos = *pos + 1;

     
    ch++;

    while (*ch == '0') {
        num_pos++;
        ch++;
    }

    while ((*ch == '0' || *ch == '1') && num_digits != max_digits) {
        num_digits++;
        result = (result * 2) + *ch - '0';
        ch++;
        num_pos++;
    }

    *pos = num_pos;
    return result;
}

static uint64_t scan_octal(int *pos, char *ch)
{
    uint64_t result = 0;
    int num_digits = 0;
    int max_digits = 23;
    int num_pos = *pos + 1;

     
    ch++;

    while (*ch == '0') {
        num_pos++;
        ch++;
    }

    while (*ch >= '0' && *ch <= '7' && num_digits != max_digits) {
        num_digits++;
        result = (result * 8) + *ch - '0';
        num_pos++;
        ch++;
    }

    *pos = num_pos;
    return result;
}

static uint64_t scan_decimal(loli_lex_state *lexer, int *pos, int *is_integer,
        char *new_ch)
{
    uint64_t result = 0;
    int num_digits = 0;
    int max_digits = 21;
     
    int num_pos = *pos;
    int have_dot = 0;

    while (*new_ch == '0') {
        num_pos++;
        new_ch++;
    }

    while (num_digits != max_digits) {
        if (*new_ch >= '0' && *new_ch <= '9') {
            if (*is_integer) {
                num_digits++;
                result = (result * 10) + *new_ch - '0';
            }
        }
        else if (*new_ch == '.') {
             
            if (have_dot == 1 ||
                isdigit(*(new_ch + 1)) == 0)
                break;

            have_dot = 1;
            *is_integer = 0;
        }
        else if (*new_ch == 'e') {
            *is_integer = 0;
            scan_exponent(lexer, &num_pos, new_ch);
            break;
        }
        else
            break;

        num_digits++;
        num_pos++;
        new_ch++;
    }

    *pos = num_pos;
    return result;
}

static uint64_t scan_hex(int *pos, char *new_ch)
{
    uint64_t result = 0;
    int num_digits = 0;
    int max_digits = 17;
    int num_pos = *pos + 1;

     
    new_ch++;

    while (*new_ch == '0') {
        num_pos++;
        new_ch++;
    }

    while (num_digits != max_digits) {
        char mod;
        if (*new_ch >= '0' && *new_ch <= '9')
            mod = '0';
        else if (*new_ch >= 'a' && *new_ch <= 'f')
            mod = 'a' - 10;
        else if (*new_ch >= 'A' && *new_ch <= 'F')
            mod = 'A' - 10;
        else
            break;

        result = (result * 16) + *new_ch - mod;
        num_digits++;
        num_pos++;
        new_ch++;
    }

    *pos = num_pos;
    return result;
}

static void scan_number(loli_lex_state *lexer, int *pos, loli_token *tok,
        char *new_ch)
{
    char sign = *new_ch;
    int num_pos = *pos;
    int num_start = *pos;
    int is_integer = 1;
    uint64_t integer_value = 0;
    loli_raw_value yield_val;

    if (sign == '-' || sign == '+') {
        num_pos++;
        new_ch++;
    }

    if (*new_ch == '0') {
        num_pos++;
        new_ch++;

        if (*new_ch == 'b')
            integer_value = scan_binary(&num_pos, new_ch);
        else if (*new_ch == 'c')
            integer_value = scan_octal(&num_pos, new_ch);
        else if (*new_ch == 'x')
            integer_value = scan_hex(&num_pos, new_ch);
        else
            integer_value = scan_decimal(lexer, &num_pos, &is_integer, new_ch);
    }
    else
        integer_value = scan_decimal(lexer, &num_pos, &is_integer, new_ch);

    char suffix = lexer->input_buffer[num_pos];

    if (suffix == 't') {
        if (is_integer == 0)
            loli_raise_syn(lexer->raiser, "Double value with Byte suffix.");

        if (sign == '-' || sign == '+')
            loli_raise_syn(lexer->raiser, "Byte values cannot have a sign.");

        if (integer_value > 0xFF)
            loli_raise_syn(lexer->raiser, "Byte value is too large.");

        lexer->last_integer = integer_value;
        *tok = tk_byte;
        num_pos++;
    }
    else if (is_integer) {
         
        yield_val.doubleval = 0.0;
        if (sign != '-') {
            if (integer_value <= INT64_MAX)
                yield_val.integer = (int64_t)integer_value;
            else
                loli_raise_syn(lexer->raiser, "Integer value is too large.");
        }
        else {
             
            uint64_t max = 9223372036854775808ULL;
            if (integer_value <= max)
                yield_val.integer = -(int64_t)integer_value;
            else
                loli_raise_syn(lexer->raiser, "Integer value is too small.");
        }

        lexer->last_integer = yield_val.integer;
        *tok = tk_integer;
    }
     
    else {
        double double_result;
        char *input_buffer = lexer->input_buffer;
        int str_size = num_pos - num_start;
        strncpy(lexer->label, input_buffer+num_start, str_size * sizeof(char));

        lexer->label[str_size] = '\0';
        errno = 0;
        double_result = strtod(lexer->label, NULL);
        if (errno == ERANGE)
            loli_raise_syn(lexer->raiser, "Double value is out of range.");

        yield_val.doubleval = double_result;
        lexer->last_literal = loli_get_double_literal(lexer->symtab,
                yield_val.doubleval);
        *tok = tk_double;
    }

    *pos = num_pos;
}

static void scan_multiline_comment(loli_lex_state *lexer, char **source_ch)
{
    int start_line = lexer->line_num;
     
    char *new_ch = *source_ch + 2;

    while (1) {
        if (*new_ch == ']' &&
            *(new_ch + 1) == '#') {
            new_ch += 2;
            break;
        }
        else if (*new_ch == '\n') {
            if (read_line(lexer)) {
                new_ch = &(lexer->input_buffer[0]);
                 
                continue;
            }
            else {
                loli_raise_syn(lexer->raiser,
                           "Unterminated multi-line comment (started at line %d).",
                           start_line);
            }
        }

        new_ch++;
    }

    *source_ch = new_ch;
}

static void ensure_label_size(loli_lex_state *lexer, int at_least)
{
    if (lexer->label_size > at_least)
        return;

    int new_size = lexer->label_size;
    while (new_size < at_least)
        new_size *= 2;

    char *new_data = loli_realloc(lexer->label, new_size * sizeof(*new_data));

    lexer->label = new_data;
    lexer->label_size = new_size;
}

static void scan_docstring(loli_lex_state *lexer, char **ch)
{
    int offset = (int)(*ch - lexer->input_buffer);
    char *buffer = lexer->input_buffer;

    while (1) {
        int i = 0;

        while (buffer[i] == ' ' || buffer[i] == '\t')
            i++;

        if (buffer[i] != '#')
            break;
        else if (buffer[i] == '#') {
            if (buffer[i + 1] != '#' ||
                buffer[i + 2] != '#')
                loli_raise_syn(lexer->raiser,
                        "Docstring line does not start with full '###'.");
            else if (i != offset)
                loli_raise_syn(lexer->raiser,
                        "Docstring has inconsistent indentation.");
        }

        if (read_line(lexer))
             
            buffer = lexer->input_buffer;
        else
            break;
    }
}

static void scan_quoted_raw(loli_lex_state *, char **, int *, int);

#define SQ_IS_BYTESTRING 0x01
#define SQ_IN_LAMBDA     0x02

static void scan_quoted_raw(loli_lex_state *lexer, char **source_ch, int *start,
        int flags)
{
    char *label;
    int label_pos, multiline_start = 0;
    int is_multiline = 0;

    char *new_ch = *source_ch;
    label = lexer->label;

     
    if (*(new_ch + 1) == '"' &&
        *(new_ch + 2) == '"') {
        is_multiline = 1;
        multiline_start = lexer->line_num;
        new_ch += 2;
    }

    if (flags & SQ_IN_LAMBDA) {
        int num = is_multiline ? 3 : 1;
        strncpy(lexer->label + *start, "\"\"\"", num);
        *start += num;
    }

     
    new_ch++;
    label_pos = *start;
    int backslash_before_newline = 0;

    while (1) {
        if (*new_ch == '\\') {
            char *start_ch = new_ch;

            new_ch++;

            if (*new_ch == '\n') {
                if (flags & SQ_IN_LAMBDA) {
                    label[label_pos] = '\\';
                    label_pos++;
                }

                backslash_before_newline = 1;
                continue;
            }

            char esc_ch = scan_escape(lexer, &new_ch);
             
            if ((flags & SQ_IS_BYTESTRING) == 0 &&
                (esc_ch == 0 || (unsigned char)esc_ch > 127))
                loli_raise_syn(lexer->raiser, "Invalid escape sequence.");

            if ((flags & SQ_IN_LAMBDA) == 0) {
                label[label_pos] = esc_ch;
                label_pos++;
            }
            else {
                 
                while (start_ch != new_ch) {
                    label[label_pos] = *start_ch;
                    label_pos++;
                    start_ch++;
                }
            }
        }
        else if (*new_ch == '\n') {
            if (is_multiline == 0 && backslash_before_newline == 0)
                loli_raise_syn(lexer->raiser, "Newline in single-line string.");
            int line_length = read_line(lexer);
            if (line_length == 0) {
                loli_raise_syn(lexer->raiser,
                           "Unterminated string (started at line %d).",
                           multiline_start);
            }

            ensure_label_size(lexer, label_pos + line_length + 3);
            label = lexer->label;
            new_ch = &lexer->input_buffer[0];

            if (backslash_before_newline == 0 || (flags & SQ_IN_LAMBDA)) {
                label[label_pos] = '\n';
                label_pos++;
            }
            else {
                while (*new_ch == ' ')
                    new_ch++;

                backslash_before_newline = 0;
            }
        }
        else if (*new_ch == '"' &&
                 ((is_multiline == 0) ||
                  (*(new_ch + 1) == '"' && *(new_ch + 2) == '"'))) {
            new_ch++;
            break;
        }
        else if (*new_ch == '\0')
            break;
        else {
            label[label_pos] = *new_ch;
            label_pos++;
            new_ch++;
        }
    }

    if (is_multiline)
        new_ch += 2;

    if (flags & SQ_IN_LAMBDA) {
        int num = is_multiline ? 3 : 1;
        strncpy(lexer->label + label_pos, "\"\"\"", num);
        label_pos += num;
    }

    if ((flags & (SQ_IN_LAMBDA | SQ_IS_BYTESTRING)) == 0)
        label[label_pos] = '\0';

    if ((flags & SQ_IN_LAMBDA) == 0) {
        if ((flags & SQ_IS_BYTESTRING) == 0)
            lexer->last_literal = loli_get_string_literal(lexer->symtab, label);
        else
            lexer->last_literal = loli_get_bytestring_literal(lexer->symtab,
                    label, label_pos);
    }

    *source_ch = new_ch;
    *start = label_pos;
}

static void scan_quoted(loli_lex_state *lexer, char **source_ch, int flags)
{
    int dummy = 0;
    scan_quoted_raw(lexer, source_ch, &dummy, flags);
}

static void scan_single_quote(loli_lex_state *lexer, char **source_ch)
{
    char *new_ch = *source_ch + 1;
    char ch = *new_ch;

    if (ch == '\\') {
        new_ch++;
        ch = scan_escape(lexer, &new_ch);
    }
    else if (ch != '\'') {
        ch = *new_ch;
        new_ch += 1;
    }
    else
        loli_raise_syn(lexer->raiser, "Byte literals cannot be empty.");

    if (*new_ch != '\'')
        loli_raise_syn(lexer->raiser, "Multi-character byte literal.");

    *source_ch = new_ch + 1;
    lexer->last_integer = (unsigned char)ch;
}

static void scan_lambda(loli_lex_state *lexer, char **source_ch)
{
    char *label = lexer->label, *ch = *source_ch;
    int brace_depth = 1, i = 0;

    lexer->expand_start_line = lexer->line_num;
    label = lexer->label;

    while (1) {
        if (*ch == '\n' ||
            (*ch == '#' &&
             *(ch + 1) != '[')) {
            int line_length = read_line(lexer);
            if (line_length == 0)
                loli_raise_syn(lexer->raiser,
                        "Unterminated lambda (started at line %d).",
                        lexer->expand_start_line);

            ensure_label_size(lexer, i + line_length + 3);
            label = lexer->label;
            ch = &lexer->input_buffer[0];
            label[i] = '\n';
            i++;
            continue;
        }
        else if (*ch == '#' &&
                 *(ch + 1) == '[') {
            int saved_line_num = lexer->line_num;
            scan_multiline_comment(lexer, &ch);

             
            if (saved_line_num != lexer->line_num) {
                int increase = lexer->line_num - saved_line_num;
                 
                ensure_label_size(lexer,
                        i + increase + 3 + strlen(lexer->input_buffer));
                label = lexer->label;

                memset(label + i, '\n', increase);
                i += increase;
            }
            continue;
        }
        else if (*ch == '"') {
            int flags = SQ_IN_LAMBDA;

             
            if (ch != &lexer->input_buffer[0] && *(ch - 1) == 'b')
                flags |= SQ_IS_BYTESTRING;

            scan_quoted_raw(lexer, &ch, &i, flags);
            label = lexer->label;
             
            continue;
        }
        else if (*ch == '\'') {
            scan_single_quote(lexer, &ch);
            ensure_label_size(lexer, i + 7);
            label = lexer->label;

            char buffer[8];
            sprintf(buffer, "'\\%d'", (uint8_t)lexer->last_integer);
            strcpy(label + i, buffer);
            i += strlen(buffer);
            continue;
        }
        else if (*ch == '(')
            brace_depth++;
        else if (*ch == ')') {
            if (brace_depth == 1)
                break;

            brace_depth--;
        }

        label[i] = *ch;
        ch++;
        i++;
    }

     
    label[i] = '\0';

    *source_ch = ch + 1;
}

void loli_lexer_verify_path_string(loli_lex_state *lexer)
{
    char *label = lexer->label;

    if (label[0] == '\0')
        loli_raise_syn(lexer->raiser, "Import path must not be empty.");

    int original_len = strlen(lexer->label);
    int len = original_len;
    int necessary = 0;
    char *reverse_iter = &lexer->input_buffer[lexer->input_pos - 2];
    char *reverse_label = label + len - 1;

    if (lexer->input_pos > 3 &&
        *reverse_iter == '"' &&
        *(reverse_iter - 1) != '\\')
        loli_raise_syn(lexer->raiser,
                "Import path cannot be a triple-quote string.");

    if (*reverse_label == '/' || *label == '/')
        loli_raise_syn(lexer->raiser,
                "Import path cannot begin or end with '/'.");

    while (len) {
        if (*reverse_iter != *reverse_label)
            loli_raise_syn(lexer->raiser,
                    "Import path cannot contain escape characters.");

        char label_ch = *reverse_label;

        if (ident_table[(unsigned char)label_ch] == 0) {
            necessary = 1;
            if (label_ch == '/')
                *reverse_label = LOLI_PATH_CHAR;
        }

        reverse_iter--;
        reverse_label--;
        len--;
    }

    if (necessary == 0)
        loli_raise_syn(lexer->raiser,
                "Simple import paths do not need to be quoted.");
}

int loli_lexer_digit_rescan(loli_lex_state *lexer)
{
    int pos = lexer->input_pos - 1;
    char *input = lexer->input_buffer;
    char ch = ' ';

    while (pos) {
        ch = input[pos];
        if (isalnum(ch) == 0)
            break;

        pos--;
    }

    int plus_or_minus = (ch == '-' || ch == '+');

    if (plus_or_minus) {
        lexer->input_pos = pos + 1;
        loli_lexer(lexer);
    }

    return plus_or_minus;
}


void loli_grow_lexer_buffers(loli_lex_state *lexer)
{
    int new_size = lexer->input_size;
    new_size *= 2;

     
    if (lexer->label_size == lexer->input_size) {
        char *new_label;
        new_label = loli_realloc(lexer->label, new_size * sizeof(*new_label));

        lexer->label = new_label;
        lexer->label_size = new_size;
    }

    char *new_lb;
    new_lb = loli_realloc(lexer->input_buffer, new_size * sizeof(*new_lb));

    lexer->input_buffer = new_lb;
    lexer->input_size = new_size;
}

void loli_lexer_load(loli_lex_state *lexer, loli_lex_entry_type entry_type,
        const void *source)
{
    loli_lex_entry *entry = get_entry(lexer);

    entry->extra = NULL;
    entry->entry_type = entry_type;
    entry->final_token = tk_eof;

    if (entry_type != et_file &&
        entry_type != et_shallow_string) {
        char *str_source = (char *)source;
        char *copy = loli_malloc((strlen(str_source) + 1) * sizeof(*copy));

        strcpy(copy, str_source);
        source = copy;
        entry->extra = copy;

        entry->final_token = tk_end_lambda;
    }

    entry->source = (void *)source;
    read_line(lexer);
}

void loli_lexer(loli_lex_state *lexer)
{
    char *ch_class;
    int input_pos = lexer->input_pos;
    loli_token token;

    ch_class = lexer->ch_class;

    while (1) {
        char *start_ch, *ch;
        int group;

        ch = &lexer->input_buffer[input_pos];
        start_ch = ch;

        while (*ch == ' ' || *ch == '\t')
            ch++;

        input_pos += ch - start_ch;
        group = ch_class[(unsigned char)*ch];
        if (group == CC_WORD) {
            token = tk_word;

            label_handling: ;

             
            int word_pos = 0;
            char *label = lexer->label;

            do {
                label[word_pos] = *ch;
                word_pos++;
                ch++;
            } while (ident_table[(unsigned char)*ch]);
            input_pos += word_pos;
            label[word_pos] = '\0';
        }
        else if (group == CC_COLON) {
            if (ident_table[(unsigned char)*(ch + 1)]) {
                ch++;
                input_pos++;
                token = tk_keyword_arg;
                goto label_handling;
            }
            else {
                input_pos++;
                token = group;
            }
        }
        else if (group <= CC_G_ONE_LAST) {
            input_pos++;
             
            token = group;
        }
        else if (group == CC_NEWLINE) {
            if (read_line(lexer)) {
                input_pos = 0;
                continue;
            }
            else {
                token = lexer->entry->final_token;
                input_pos = 0;
            }
        }
        else if (group == CC_SHARP) {
            if (*(ch + 1) == '[') {
                scan_multiline_comment(lexer, &ch);
                input_pos = ch - lexer->input_buffer;
                continue;
            }
            else if (*(ch + 1) == '#' &&
                     *(ch + 2) == '#') {
                scan_docstring(lexer, &ch);
                input_pos = ch - lexer->input_buffer;
                token = tk_docstring;
            }
            else if (read_line(lexer)) {
                input_pos = 0;
                continue;
            }
             
            else {
                token = lexer->entry->final_token;
                input_pos = 0;
            }
        }
        else if (group == CC_DOUBLE_QUOTE) {
            scan_quoted(lexer, &ch, 0);
            input_pos = ch - lexer->input_buffer;
            token = tk_double_quote;
        }
        else if (group == CC_SINGLE_QUOTE) {
            scan_single_quote(lexer, &ch);
            input_pos = ch - lexer->input_buffer;
            token = tk_byte;
        }
        else if (group == CC_B) {
            if (*(ch + 1) == '"') {
                ch++;
                input_pos++;
                scan_quoted(lexer, &ch, SQ_IS_BYTESTRING);
                input_pos = ch - lexer->input_buffer;
                token = tk_bytestring;
            }
            else {
                token = tk_word;
                goto label_handling;
            }
        }
        else if (group <= CC_G_TWO_LAST) {
            input_pos++;
            if (lexer->input_buffer[input_pos] == '=') {
                input_pos++;
                token = grp_two_eq_table[group - CC_G_TWO_OFFSET];
            }
            else
                token = grp_two_table[group - CC_G_TWO_OFFSET];
        }
        else if (group == CC_NUMBER)
            scan_number(lexer, &input_pos, &token, ch);
        else if (group == CC_DOT) {
            if (ch_class[(unsigned char)*(ch + 1)] == CC_NUMBER)
                scan_number(lexer, &input_pos, &token, ch);
            else {
                ch++;
                input_pos++;
                token = tk_dot;
                if (*ch == '.') {
                    ch++;
                    input_pos++;

                    if (*ch == '.') {
                        input_pos++;
                        token = tk_three_dots;
                    }
                    else
                        loli_raise_syn(lexer->raiser,
                                "'..' is not a valid token (expected 1 or 3 dots).");
                }
            }
        }
        else if (group == CC_PLUS) {
            if (ch_class[(unsigned char)*(ch + 1)] == CC_NUMBER)
                scan_number(lexer, &input_pos, &token, ch);
            else if (*(ch + 1) == '=') {
                ch += 2;
                input_pos += 2;
                token = tk_plus_eq;
            }
            else if (*(ch + 1) == '+') {
                if (*(ch + 2) == '=')
                    loli_raise_syn(lexer->raiser,
                            "'++=' is not a valid token.");

                ch += 2;
                input_pos += 2;
                token = tk_plus_plus;
            }
            else {
                ch++;
                input_pos++;
                token = tk_plus;
            }
        }
        else if (group == CC_MINUS) {
            if (ch_class[(unsigned char)*(ch + 1)] == CC_NUMBER)
                scan_number(lexer, &input_pos, &token, ch);
            else if (*(ch + 1) == '=') {
                ch += 2;
                input_pos += 2;
                token = tk_minus_eq;
            }
            else {
                ch++;
                input_pos++;
                token = tk_minus;
            }
        }
        else if (group == CC_LEFT_PARENTH) {
            input_pos++;
            ch++;
            if (*ch == '|') {
                scan_lambda(lexer, &ch);
                input_pos = ch - lexer->input_buffer;
                token = tk_lambda;
            }
            else
                token = tk_left_parenth;
        }
        else if (group == CC_AMPERSAND) {
            input_pos++;
            ch++;

            if (*ch == '&') {
                input_pos++;
                ch++;
                token = tk_logical_and;
            }
            else if (*ch == '=') {
                input_pos++;
                ch++;
                token = tk_bitwise_and_eq;
            }
            else
                token = tk_bitwise_and;
        }
        else if (group == CC_VBAR) {
            input_pos++;
            ch++;

            if (*ch == '|') {
                input_pos++;
                ch++;
                token = tk_logical_or;
            }
            else if (*ch == '>') {
                input_pos++;
                ch++;
                token = tk_func_pipe;
            }
            else if (*ch == '=') {
                input_pos++;
                ch++;
                token = tk_bitwise_or_eq;
            }
            else
                token = tk_bitwise_or;
        }
        else if (group == CC_GREATER || group == CC_LESS) {
             
            input_pos++;
            if (group == CC_GREATER)
                token = tk_gt;
            else
                token = tk_lt;

            ch++;
            if (*ch == '=') {
                token++;
                input_pos++;
            }
            else if (*ch == *(ch - 1)) {
                input_pos++;
                ch++;
                if (*ch == '=') {
                     
                    input_pos++;
                    token += 3;
                }
                else
                     
                    token += 2;
            }
            else if (*ch == '[' && token == tk_lt) {
                input_pos++;
                token = tk_tuple_open;
            }
        }
        else if (group == CC_EQUAL) {
            input_pos++;
            if (lexer->input_buffer[input_pos] == '=') {
                token = tk_eq_eq;
                input_pos++;
            }
            else if (lexer->input_buffer[input_pos] == '>') {
                token = tk_arrow;
                input_pos++;
            }
            else
                token = tk_equal;
        }
        else if (group == CC_RIGHT_BRACKET) {
            input_pos++;
            ch++;
            if (*ch == '>') {
                input_pos++;
                token = tk_tuple_close;
            }
            else
                token = tk_right_bracket;
        }
        else if (group == CC_AT) {
            ch++;
            input_pos++;
            if (*ch == '(') {
                input_pos++;
                token = tk_typecast_parenth;
            }
            else if (ch_class[(unsigned char)*ch] == CC_WORD ||
                     *ch == 'b') {
                 
                char *label = lexer->label;
                int word_pos = 0;

                do {
                    label[word_pos] = *ch;
                    word_pos++;
                    ch++;
                } while (ident_table[(unsigned char)*ch]);
                input_pos += word_pos;
                label[word_pos] = '\0';
                token = tk_prop_word;
            }
            else
                token = tk_invalid;
        }
        else if (group == CC_QUESTION) {
            ch++;
            input_pos++;
            if (*ch == '>') {
                input_pos++;
                token = tk_end_tag;
            }
            else
                token = tk_invalid;
        }
        else if (group == CC_DOLLAR) {
            ch++;
            input_pos++;
            if (*ch == '1') {
                input_pos++;
                lexer->last_integer = 1;
                token = tk_scoop;
            }
            else
                token = tk_invalid;
        }
        else
            token = tk_invalid;

        lexer->input_pos = input_pos;
        lexer->token = token;

        return;
    }
}

void loli_verify_template(loli_lex_state *lexer)
{
    if (strncmp(lexer->input_buffer, "<?loli", 5) != 0)
        loli_raise_syn(lexer->raiser,
                "Files in template mode must start with '<?loli'.");

    lexer->input_pos = 6;
}

int loli_lexer_read_content(loli_lex_state *lexer, char **out_buffer)
{
    char c;
    int lbp, htmlp;

     
    lbp = lexer->input_pos;
    c = lexer->input_buffer[lbp];
    htmlp = 0;

     
    if (c == '\n' &&
        lbp > 2 &&
        lexer->input_buffer[lbp - 1] == '>' &&
        lexer->input_buffer[lbp - 2] == '?') {

        goto next_line;
    }

     
    while (1) {
        lbp++;
        if (c == '<') {
            if (strncmp(lexer->input_buffer + lbp, "?loli", 5) == 0) {
                lexer->label[htmlp] = '\0';
                *out_buffer = lexer->label;
                lexer->input_pos = lbp + 5;
                return 0;
            }
        }
        lexer->label[htmlp] = c;
        htmlp++;
        if (htmlp == (lexer->input_size - 1)) {
            lexer->label[htmlp] = '\0';
            *out_buffer = lexer->label;
            lexer->input_pos = lbp;
            return 1;
        }

        if (c == '\n') {
next_line:
            if (read_line(lexer))
                lbp = 0;
            else {
                lexer->token = lexer->entry->final_token;

                 
                if (htmlp == 1 && lexer->label[0] == '\n')
                    htmlp = 0;

                lexer->label[htmlp] = '\0';
                *out_buffer = lexer->label;
                lexer->input_pos = 0;
                return 0;
            }
        }

        c = lexer->input_buffer[lbp];
    }
}

char *tokname(loli_token t)
{
    static char *toknames[] =
    {")", ",", "{", "}", "[", ":", "~", "^", "^=", "!", "!=", "%", "%=", "*",
     "*=", "/", "/=", "+", "+=", "++", "-", "-=", "<", "<=", "<<", "<<=", ">",
     ">=", ">>", ">>=", "=", "==", "(", "a lambda", "<[", "]>", "]", "=>",
     "a label", "a property name", "a string", "a bytestring", "a byte",
     "an integer", "a double", "a docstring", "a named argument", ".", "&",
     "&=", "&&", "|", "|=", "||", "@(", "...", "|>", "$1", "invalid token",
     "end of lambda", "?>", "end of file"};
    char *result = NULL;

    if (t < (sizeof(toknames) / sizeof(toknames[0])))
        result = toknames[t];

    return result;
}
