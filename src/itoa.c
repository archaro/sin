#include <limits.h>

char* itoa(int value, char* buffer, int base) {
    // Handle negative numbers
    if (value < 0 && base == 10) {
        *buffer++ = '-';  // Add minus sign
        value = -value;   // Make the value positive for conversion
    }

    // Efficiently convert digits using a lookup table
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char* p = buffer;
    int temp = value;
    do {
        int digit = temp % base;
        *p++ = digits[digit];
        temp /= base;
    } while (temp);

    // Reverse the string in-place
    *p-- = '\0';
    while (buffer < p) {
        char c = *buffer;
        *buffer++ = *p;
        *p-- = c;
    }

    return buffer;
}

