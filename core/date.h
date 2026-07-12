#ifndef LOWTASK_CORE_DATE_H
#define LOWTASK_CORE_DATE_H

#include <stdbool.h>

#define LOWTASK_DATE_LENGTH 10U

bool date_is_valid(const char *date);
int date_compare(const char *left, const char *right);
bool date_add_days(const char *source, unsigned int days, char output[LOWTASK_DATE_LENGTH + 1U]);
bool date_is_next_day(const char *date, const char *candidate);

#endif
