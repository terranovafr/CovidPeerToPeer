struct tm getMinRegister();
struct tm getMaxRegister();
struct tm getFirstDate(struct tm tm1, struct tm tm2);
struct tm getLastDate(struct tm tm1, struct tm tm2);
void addDay(struct tm* dateAndTime, const int daysToAdd);