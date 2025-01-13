# Common design decisions (not necessarily optimal)


## Usual workflow
The schema is created inside a `sample` call if it does not exist. The set is also created in the same way. 


## timestamp
The sets often have additional timestamp related fields. They contain the information from the Lustre files. 
In normal situations, these additional fields are very close to the LDMS timestamp.
A large difference between the LDMS timestamp and the Lustre timestamp probably indicates a stale Lustre metric file or something.
An alternative solution to identify such cases within the samplers might be better.

## "Destination"
Many of the metrics have a "producer" and a "destination" ("server", "protocol", "nid", "client", "OST", "MDT", etc.).

## Order within the schema
While not always consistent, the record is often the first field in the schema and the list is the last field.