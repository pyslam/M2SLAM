/* This file was generated by ODB, object-relational mapping (ORM)
 * compiler for C++.
 */

DROP TABLE IF EXISTS "Data_TopoMapPoint" CASCADE;

CREATE TABLE "Data_TopoMapPoint" (
  "id" BIGSERIAL NOT NULL PRIMARY KEY,
  "topoId" BIGINT NOT NULL,
  "pose" TEXT NOT NULL,
  "data" TEXT NOT NULL);

