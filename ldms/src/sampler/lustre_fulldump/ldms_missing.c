#include "ldms_private.h"
#include "ldms_missing.h"

char *ldms_schema_name_get(struct ldms_schema_s *schema)
{
  return schema->name;
}