#include "xps_directory.h"

#define BYTE 0
#define KILOBYTE 1
#define MEGABYTE 2
#define GIGABYTE 3
#define TERABYTE 4

char *size_unit[] = {"B", "KB", "MB", "KB", "TB"};

xps_buffer_t *xps_directory_browsing(const char *dir_path,
									 const char *pathname) {
	assert(dir_path != NULL);
	assert(dir_path != pathname);

	xps_buffer_t *directory_browsing =
		xps_buffer_create(DEFAULT_BUFFER_SIZE, 0, NULL);
	if (directory_browsing == NULL) {
		logger(LOG_ERROR, "xps_directory_browsing()",
			   "xps_buffer_create() returned NULL");
		return NULL;
	}

	// Here we will append the html file into this buff starting from the
	// heading and basic styling
	xps_buffer_append(
		directory_browsing,
		"<html><head lang='en'>"
		"<meta http-equiv='Content-Type' content='text/html; charset=UTF-8'>"
		"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
		"<title>Directory: %s</title>"
		"<style>body{font-family: monospace; font-size: 15px;} "
		"td {padding: 1.5px 6px; padding-right: 20px;} "
		"h1{font-family: serif; margin: 0;} "
		"h3{font-family: serif;margin: 12px 0px;"
		"background-color: rgba(0,0,0,0.1); padding: 4px 0px;} "
		"</style></head><body><h1>eXpServer</h1>"
		"<h3>Index of&nbsp;%s</h3><hr><table>",
		pathname, pathname);

	// Open a directory stream using opendir function
	DIR *dir = opendir(dir_path);
	if (dir == NULL) {
		logger(LOG_ERROR, "xps_directory_browsing()", "opendir() failed");
		xps_buffer_destroy(directory_browsing);
		return NULL;
	}

	struct dirent *dir_entry;

	while ((dir_entry = readdir(dir)) != NULL) {
		// skip the first two entries such as . and .. in list directory from
		// dir_entry->d_dname
		if (strcmp(dir_entry->d_name, ".") == 0 ||
			strcmp(dir_entry->d_name, "..") == 0)
			continue;

		char full_path[1024];
		/* copy the dir_path and dir_entry->d_name with formatted with a / into
		 * full_path */
		size_t dir_path_len = strlen(dir_path);
		snprintf(full_path, 1024, "%s/%s", dir_path, dir_entry->d_name);

		// open the file in the full_path
		int file_fd = open(full_path, O_RDONLY);
		if (file_fd == -1) {
			logger(LOG_ERROR, "xps_directory_browsing()",
				   "failed to open file");
			continue;
		}

		struct stat file_stat;

		// get information about the file
		if (fstat(file_fd, &file_stat) == -1) {
			logger(LOG_ERROR, "xps_directory_browsing()",
				   "fstat() failed. Unable to get file info");
			close(file_fd);
			continue;
		}
		// check if file is regular file or if the file is a direcory
		if (S_ISREG(file_stat.st_mode) || S_ISDIR(file_stat.st_mode)) {
			char *is_dir = S_ISDIR(file_stat.st_mode) ? "/" : "";

			char *temp_pathname = str_create(pathname);
			if (temp_pathname[strlen(temp_pathname) - 1] == '/')
				temp_pathname[strlen(temp_pathname) - 1] = '\0';

			char time_buff[20];
			strftime(time_buff, sizeof(time_buff), "%Y-%m-%d %H:%M:%S",
					 localtime(&file_stat.st_mtime));

			if (S_ISREG(file_stat.st_mode)) { // IS_FILE
				double filesize = (double)file_stat.st_size;
				int filesize_unit = BYTE;
				while (filesize >= 1024.0 && filesize_unit < TERABYTE) {
					filesize /= 1024.0;
					filesize_unit++;
				}

				char size_buff[10];
				snprintf(size_buff, sizeof(size_buff),
						 filesize_unit == 0 ? "%.0f %s" : "%.2f %s", filesize,
						 size_unit[filesize_unit]);

				xps_buffer_append(directory_browsing,
								  "<tr><td><a href='%s/%s'>%s%s</a></td>"
								  "<td>%s</td><td>%s</td></tr>\n",
								  temp_pathname, dir_entry->d_name,
								  dir_entry->d_name, is_dir, size_buff,
								  time_buff);
			} else
				xps_buffer_append(
					directory_browsing,
					"<tr><td><a "
					"href='%s/%s'>%s%s</a></td><td></td><td>%s</td></tr>\n",
					temp_pathname, dir_entry->d_name, dir_entry->d_name, is_dir,
					time_buff);
			free(temp_pathname);
		}

		close(file_fd);
	}

	closedir(dir);
	xps_buffer_append(directory_browsing, "</table></body></html>");

	return directory_browsing;
}
