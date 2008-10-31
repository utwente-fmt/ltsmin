#ifndef CONFIG_H_
#define CONFIG_H_

#define _GNU_SOURCE
#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE64_SOURCE

#define _XOPEN_SOURCE 600

#ifndef DEFFILEMODE
#define DEFFILEMODE 0666
#endif

#define LTSMIN_PATHNAME_MAX 1024

#endif /* CONFIG_H_ */
