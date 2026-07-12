#include "core/date.h"

#include <stddef.h>
#include <string.h>

static size_t bounded_length(const char *text, size_t limit) {
    size_t length = 0U;
    if (text == NULL) {
        return 0U;
    }
    while (length < limit && text[length] != '\0') {
        ++length;
    }
    return length;
}

static bool leap_year(unsigned int year) {
    return year % 400U == 0U || (year % 4U == 0U && year % 100U != 0U);
}

static unsigned int month_days(unsigned int year, unsigned int month) {
    static const unsigned int days[] = {31U, 28U, 31U, 30U, 31U, 30U,
                                        31U, 31U, 30U, 31U, 30U, 31U};
    return month == 2U && leap_year(year) ? 29U : days[month - 1U];
}

static unsigned int days_before_year(unsigned int year) {
    const unsigned int preceding_years = year - 1U;
    return preceding_years * 365U + preceding_years / 4U - preceding_years / 100U +
           preceding_years / 400U;
}

static unsigned int date_ordinal(unsigned int year, unsigned int month, unsigned int day) {
    unsigned int ordinal = days_before_year(year) + day - 1U;
    for (unsigned int current_month = 1U; current_month < month; ++current_month) {
        ordinal += month_days(year, current_month);
    }
    return ordinal;
}

static void parse_date(const char *date, unsigned int *year, unsigned int *month, unsigned int *day) {
    *year = (unsigned int)(date[0] - '0') * 1000U + (unsigned int)(date[1] - '0') * 100U +
            (unsigned int)(date[2] - '0') * 10U + (unsigned int)(date[3] - '0');
    *month = (unsigned int)(date[5] - '0') * 10U + (unsigned int)(date[6] - '0');
    *day = (unsigned int)(date[8] - '0') * 10U + (unsigned int)(date[9] - '0');
}

bool date_is_valid(const char *date) {
    if (bounded_length(date, LOWTASK_DATE_LENGTH + 1U) != LOWTASK_DATE_LENGTH ||
        date[4] != '-' || date[7] != '-') {
        return false;
    }
    static const size_t digit_positions[] = {0U, 1U, 2U, 3U, 5U, 6U, 8U, 9U};
    for (size_t index = 0U; index < sizeof(digit_positions) / sizeof(digit_positions[0]); ++index) {
        const char digit = date[digit_positions[index]];
        if (digit < '0' || digit > '9') {
            return false;
        }
    }
    unsigned int year = 0U;
    unsigned int month = 0U;
    unsigned int day = 0U;
    parse_date(date, &year, &month, &day);
    if (year == 0U || month == 0U || month > 12U) {
        return false;
    }
    return day > 0U && day <= month_days(year, month);
}

int date_compare(const char *left, const char *right) {
    const int comparison = strcmp(left, right);
    return (comparison > 0) - (comparison < 0);
}

bool date_add_days(const char *source, unsigned int days, char output[LOWTASK_DATE_LENGTH + 1U]) {
    if (output == NULL || !date_is_valid(source)) {
        return false;
    }

    unsigned int year = 0U;
    unsigned int month = 0U;
    unsigned int day = 0U;
    parse_date(source, &year, &month, &day);

    const unsigned int source_ordinal = date_ordinal(year, month, day);
    const unsigned int final_ordinal = days_before_year(10000U) - 1U;
    if (days > final_ordinal - source_ordinal) {
        return false;
    }

    const unsigned int target_ordinal = source_ordinal + days;
    unsigned int lower_year = 1U;
    unsigned int upper_year = 10000U;
    while (lower_year + 1U < upper_year) {
        const unsigned int middle_year = lower_year + (upper_year - lower_year) / 2U;
        if (days_before_year(middle_year) <= target_ordinal) {
            lower_year = middle_year;
        } else {
            upper_year = middle_year;
        }
    }

    year = lower_year;
    unsigned int day_of_year = target_ordinal - days_before_year(year);
    month = 1U;
    while (day_of_year >= month_days(year, month)) {
        day_of_year -= month_days(year, month);
        ++month;
    }
    day = day_of_year + 1U;

    char result[LOWTASK_DATE_LENGTH + 1U];
    result[0] = (char)('0' + year / 1000U);
    result[1] = (char)('0' + year / 100U % 10U);
    result[2] = (char)('0' + year / 10U % 10U);
    result[3] = (char)('0' + year % 10U);
    result[4] = '-';
    result[5] = (char)('0' + month / 10U);
    result[6] = (char)('0' + month % 10U);
    result[7] = '-';
    result[8] = (char)('0' + day / 10U);
    result[9] = (char)('0' + day % 10U);
    result[10] = '\0';
    memcpy(output, result, sizeof(result));
    return true;
}

bool date_is_next_day(const char *date, const char *candidate) {
    if (!date_is_valid(candidate)) return false;
    char next_date[LOWTASK_DATE_LENGTH + 1U];
    return date_add_days(date, 1U, next_date) && strcmp(next_date, candidate) == 0;
}
