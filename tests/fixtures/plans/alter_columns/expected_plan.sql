ALTER TABLE "public"."items" ALTER COLUMN "label" TYPE varchar(100) USING "label"::varchar(100);

ALTER TABLE "public"."items" ALTER COLUMN "price" TYPE numeric(12,2) USING "price"::numeric(12,2);

ALTER TABLE "public"."items" ALTER COLUMN "qty" TYPE int8 USING "qty"::int8;

ALTER TABLE "public"."items" ALTER COLUMN "note" SET DEFAULT '';

ALTER TABLE "public"."items" ALTER COLUMN "note" SET NOT NULL;

