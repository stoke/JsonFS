#define FUSE_USE_VERSION  26
   
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "cJSON.h"

cJSON **array;
int size;

#define element_exist(obj, name) !!cJSON_GetObjectItem(obj, name)
#define is_type(obj, name, typ) (cJSON_GetObjectItem(obj, name)->type == typ)

cJSON **parse_root_array(cJSON *array, int type) {
  int i;
  cJSON **ret;

  size = cJSON_GetArraySize(array);

  if (array->type != cJSON_Array)
    return NULL;

  ret = (cJSON **) malloc(sizeof(int*) * size);

  for (i = 0; i < size; i++) {
    ret[i] = cJSON_GetArrayItem(array, i);
    
    if (ret[i]->type != type && type > 0) {
      free(ret);
      return NULL;
    }
  }

  return ret;
}


static int json_getattr(const char *path, struct stat *stbuf)
{
  int res = 0, i;
  cJSON *element = NULL;

  memset(stbuf, 0, sizeof(struct stat));

  if(!strcmp(path, "/")) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
    
    return res;
  }

  for (i = 0; i < size; i++) {
    if (!element_exist(array[i], "name") || !is_type(array[i], "name", cJSON_String))
      continue;

    if (!strcmp(path+1, cJSON_GetObjectItem(array[i], "name")->valuestring)) {
      element = array[i];

      if (element_exist(array[i], "mode") && is_type(array[i], "mode", cJSON_Number))
        stbuf->st_mode = S_IFREG | cJSON_GetObjectItem(array[i], "mode")->valueint;
       else
        stbuf->st_mode = S_IFREG | 0444;

      if (element_exist(array[i], "nlink") && is_type(array[i], "nlink", cJSON_Number))
        stbuf->st_nlink = cJSON_GetObjectItem(array[i], "nlink")->valueint;
      else
        stbuf->st_nlink = 1;

      if (element_exist(array[i], "content") && is_type(array[i], "content", cJSON_String))
        stbuf->st_size = strlen(cJSON_GetObjectItem(array[i], "content")->valuestring);
      else
        stbuf->st_size = 0;

      break;
    }
  }
  
  if (!element)
    res = -ENOENT;

  return res;
}

static int json_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
  int i;

  (void) offset;
  (void) fi;

  if(strcmp(path, "/"))
      return -ENOENT;

  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);

  for (i = 0; i < size; i++) {
    if (element_exist(array[i], "name") && is_type(array[i], "name", cJSON_String)) {
      filler(buf, cJSON_GetObjectItem(array[i], "name")->valuestring, NULL, 0);
    }
  }

  return 0;
}

static int json_open(const char *path, struct fuse_file_info *fi)
{
  int i;
  cJSON *element;

  for (i = 0; i < size; i++) {
    if (!element_exist(array[i], "name") || !is_type(array[i], "name", cJSON_String))
      continue;

    if (!strcmp(path+1, cJSON_GetObjectItem(array[i], "name")->valuestring)) {
      element = array[i];

      break;
    }
  }

  if (!element)
    return -ENOENT;

  return 0;
}

static int json_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
  size_t len;
  char *str;

  (void) fi;

  int i;
  cJSON *element;

  for (i = 0; i < size; i++) {
    if (!element_exist(array[i], "name") || !is_type(array[i], "name", cJSON_String))
      continue;

    if (!strcmp(path+1, cJSON_GetObjectItem(array[i], "name")->valuestring)) {
      element = array[i];

      str = (element_exist(array[i], "content") && is_type(array[i], "content", cJSON_String)) ? cJSON_GetObjectItem(array[i], "content")->valuestring : "";

      len = strlen(str);
      
      if (offset < len) {
        if (offset + size > len)
          size = len - offset;
        
        memcpy(buf, str + offset, size);
      } else
       size = 0;

      return size;
    }
  }

  if (!element)
    return -ENOENT;

  return size;
}

static int json_write(const char *path, const char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi) {
  size_t len;
  char *str, *nstr;

  (void) fi;

  int i;
  cJSON *element;

  for (i = 0; i < size; i++) {
    if (!element_exist(array[i], "name") || !is_type(array[i], "name", cJSON_String))
      continue;

    if (!strcmp(path+1, cJSON_GetObjectItem(array[i], "name")->valuestring)) {
      element = array[i];

      if (!element_exist(array[i], "content"))
        cJSON_AddStringToObject(array[i], "content", "");

      len = strlen(buf);
      nstr = (char *) malloc(len);
      strcpy(nstr, cJSON_GetObjectItem(array[i], "content")->valuestring);

      cJSON_GetObjectItem(array[i], "content")->valuestring = nstr;
      str = cJSON_GetObjectItem(array[i], "content")->valuestring;

      if (offset < len) {
        if (offset + size > len)
          size = len - offset;
        
        memcpy(str + offset, buf, size);
      } else
       size = 0;

      return size;
    }
  }

  if (!element)
    return -ENOENT;

  return size;
}

static struct fuse_operations json_oper = {
  .getattr   = json_getattr,
  .readdir = json_readdir,
  .open   = json_open,
  .read   = json_read,
  .write  = json_write,
};

int main(int argc, char *argv[])
{
  FILE *fd;
  int desc, value, x;
  struct stat fs;
  char *content;
  cJSON *format, *root;
  char *args[1];

  if (argc < 3) {
    puts("./jsonmounter <mount> <json>");

    return 1;
  }

  args[0] = argv[1]; // fuse optparser inhibition

  desc = open(argv[2], O_RDONLY);

  if (desc < 0) {
    puts("OOPPS");
    return 1;
  }

  fstat(desc, &fs);
  close(desc);
  
  fd = fopen(argv[2], "r");

  content = (char *) malloc(fs.st_size);

  fread(content, fs.st_size, 1, fd);

  root = cJSON_Parse(content);

  if (!root || root->type != cJSON_Array) {
    puts("MUSTBEARRAY");
    return 1;
  }

  array = parse_root_array(root, cJSON_Object);

  if (!array) {
    puts("something wrong in the json");
    return 0;
  }
  
  free(content);
  return fuse_main(2, argv, &json_oper, NULL);
}