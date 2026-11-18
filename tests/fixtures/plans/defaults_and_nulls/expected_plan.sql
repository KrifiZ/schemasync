ALTER TABLE "public"."settings" ALTER COLUMN "theme" DROP DEFAULT;

ALTER TABLE "public"."settings" ALTER COLUMN "retries" SET DEFAULT 3;

ALTER TABLE "public"."settings" ALTER COLUMN "retries" DROP NOT NULL;

