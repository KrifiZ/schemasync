ALTER TABLE "public"."users" ADD COLUMN "name" varchar(100);

ALTER TABLE "public"."users" ADD COLUMN "active" bool NOT NULL DEFAULT true;

ALTER TABLE "public"."users" ADD COLUMN "score" numeric(10,2) DEFAULT 0;

ALTER TABLE "public"."users" ADD COLUMN "token" uuid DEFAULT gen_random_uuid();

ALTER TABLE "public"."users" ADD COLUMN "ticket" bigserial NOT NULL;

