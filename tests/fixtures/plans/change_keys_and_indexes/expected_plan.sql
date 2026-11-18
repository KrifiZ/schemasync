ALTER TABLE "public"."t" DROP CONSTRAINT "t_b_key";

ALTER TABLE "public"."t" DROP CONSTRAINT "t_pkey";

DROP INDEX "public"."t_a_idx";

ALTER TABLE "public"."t" ALTER COLUMN "a" SET NOT NULL;

ALTER TABLE "public"."t" ADD CONSTRAINT "t_pkey" PRIMARY KEY ("id", "a");

ALTER TABLE "public"."t" ADD CONSTRAINT "t_b_c_key" UNIQUE ("b", "c");

CREATE INDEX "t_a_idx" ON "public"."t" ("a", "b");

CREATE UNIQUE INDEX "t_c_idx" ON "public"."t" ("c");

