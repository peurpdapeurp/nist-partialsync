0) Make diagram for 
1) Debug why full sync is not working in larger topologies - DONE
   Only part remaining is how to pending satisfy sync interest on receiving sync data
   when it has multiple updates, example: /nodeA/2, /nodeB/4, /nodeC/5 in the data.
   Current behavior is not satisfying anything.

   Also currently we send another sync interest on any type of Nack rcvd in 500 + jitter seconds
2) Add comments and make diagram - ascertain what exactly is the current version of PSync that is working
3) Test Partial Sync part
4) Handle Application Nack in Consumer onSyncData
5) Write unit tests
6) Need to update waf to 2.0.6 (https://gerrit.named-data.net/#/c/ChronoSync/+/4614)

Need to use Murmurhash as library instead of bringing the code into our code base
