#include "timeUtility.h"
#include "vars.h"

/* getMinRegister: restituisce la data più remota per cui si ha un registro chiuso, se esiste*/

struct tm getMinRegister(){
	DIR *d;
  	struct dirent *dir;
	struct tm time;
	struct tm min;
	char dirname[20];
	char fullname[50];
	struct stat statRes;
	mode_t bits;
	sprintf(dirname, "registers/%d/", MyPort);
  	d = opendir(dirname);
	min.tm_mon = min.tm_mday = min.tm_year = 0;
  	if (d) {
		do {
			dir = readdir(d);
			if(dir == NULL)
				break;	
			// non includo nel calcolo eventuali file nascosti e i file degli aggregati ovviamente
			if(strncmp(dir->d_name, ".", 1) != 0 && strncmp(dir->d_name, "total.bin", 9) != 0 && strncmp(dir->d_name, "diff.bin", 8) != 0 && strncmp(dir->d_name, "sum.bin", 7) != 0){
				sprintf(fullname, "registers/%d/%s", MyPort, dir->d_name);
				if(stat(fullname, &statRes) < 0) printf("Errore!\n");
				bits = statRes.st_mode;
				if((bits & S_IWUSR) == 0){
					sscanf(dir->d_name, "%02d%02d%d.bin",&min.tm_mday,&min.tm_mon,&min.tm_year);
				}
			}
		} while(strncmp(dir->d_name, ".", 1) == 0 || strncmp(dir->d_name, "total.bin", 9) == 0 || strncmp(dir->d_name, "diff.bin", 8) == 0 || strncmp(dir->d_name, "sum.bin", 7) == 0 || (bits & S_IWUSR) != 0);
   		while ((dir = readdir(d)) != NULL) {
			if(strncmp(dir->d_name, ".", 1) != 0 && strncmp(dir->d_name, "total.bin", 9) != 0 && strncmp(dir->d_name, "diff.bin", 8) != 0 && strncmp(dir->d_name, "sum.bin", 7) != 0){
				sprintf(fullname, "registers/%d/%s", MyPort, dir->d_name);
				if(stat(fullname, &statRes) < 0) printf("Errore!\n");
				bits = statRes.st_mode;
				if((bits & S_IWUSR) == 0){
					sscanf(dir->d_name, "%02d%02d%d.bin",&time.tm_mday,&time.tm_mon,&time.tm_year);
					min = getFirstDate(min, time);
				}
			}
    	}
	closedir(d);
	}
	return min;
}

/* getMaxRegister: restituisce la data più recente per cui si ha un registro chiuso, se esiste*/

struct tm getMaxRegister(){
	DIR *d;
  	struct dirent *dir;
	struct tm time;
	struct tm max;
	char dirname[20];
	char fullname[50];
	struct stat statRes;
	mode_t bits;
	sprintf(dirname, "registers/%d/", MyPort);
  	d = opendir(dirname);
	max.tm_mon = max.tm_mday = max.tm_year = 0;
	if (d) {
		do {
			dir = readdir(d);
			if(dir == NULL)
				break;	
			if(strncmp(dir->d_name, ".", 1) != 0 && strncmp(dir->d_name, "total.bin", 9) != 0 && strncmp(dir->d_name, "diff.bin", 8) != 0 && strncmp(dir->d_name, "sum.bin", 8) != 0){
				sprintf(fullname, "registers/%d/%s", MyPort, dir->d_name);
				if(stat(fullname, &statRes) < 0) printf("Errore!\n");
				bits = statRes.st_mode;
				if((bits & S_IWUSR) == 0){
					sscanf(dir->d_name, "%02d%02d%d.bin",&max.tm_mday,&max.tm_mon,&max.tm_year);
				}
			}
		} while(strncmp(dir->d_name, ".", 1) == 0 || strncmp(dir->d_name, "total.bin", 9) == 0  || strncmp(dir->d_name, "diff.bin", 8) == 0 || strncmp(dir->d_name, "sum.bin", 8) == 0 || (bits & S_IWUSR) != 0); // se si tratta di . o di .. o di una cartella nascosta 
   		while ((dir = readdir(d)) != NULL) {
			if(strncmp(dir->d_name, ".", 1) != 0 && strncmp(dir->d_name, "total.bin", 9) != 0 && strncmp(dir->d_name, "diff.bin", 8) != 0 && strncmp(dir->d_name, "sum.bin", 8) != 0){
				sprintf(fullname, "registers/%d/%s", MyPort, dir->d_name);
				if(stat(fullname, &statRes) < 0) printf("Errore!\n");
				bits = statRes.st_mode;
				if((bits & S_IWUSR) == 0){
					sscanf(dir->d_name, "%02d%02d%d.bin",&time.tm_mday,&time.tm_mon,&time.tm_year);
					max = getLastDate(max, time);
				}
			}
    	}
	closedir(d);
	}
  	return max;
}

/* getLastDate: calcola la data più piccola */

struct tm getFirstDate(struct tm tm1, struct tm tm2){
	if (tm1.tm_year > tm2.tm_year)
    	return tm2;
	else if (tm1.tm_year < tm2.tm_year)
    	return tm1;
	if (tm1.tm_mon > tm2.tm_mon)
  	  return tm2;
	else if (tm1.tm_mon < tm2.tm_mon)
   	 return tm1;
	if (tm1.tm_mday > tm2.tm_mday)
   		 return tm2;
	else if (tm1.tm_mday < tm2.tm_mday)
    	return tm1;
	return tm1;
}

/* getLastDate: calcola la data più grande */

struct tm getLastDate(struct tm tm1, struct tm tm2){
	if (tm1.tm_year > tm2.tm_year)
    	return tm1;
	else if (tm1.tm_year < tm2.tm_year)
    	return tm2;
	if (tm1.tm_mon > tm2.tm_mon)
  	  return tm1;
	else if (tm1.tm_mon < tm2.tm_mon)
   	 return tm2;
	if (tm1.tm_mday > tm2.tm_mday)
   		 return tm1;
	else if (tm1.tm_mday < tm2.tm_mday)
    	return tm2;
	return tm1;
}

/* addDay: somma un giorno in maniera consistente ad una struttura di tipo tm */

void addDay(struct tm* dateAndTime, const int daysToAdd){
	dateAndTime->tm_year -= 1900;
	dateAndTime->tm_mon -=  1;
    dateAndTime->tm_mday += daysToAdd;
    mktime(dateAndTime);
	dateAndTime->tm_year += 1900;
	dateAndTime->tm_mon +=  1;
}