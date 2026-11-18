CREATE TABLE "public"."customer_loyalty_program_memberships_archive" (
    "id" uuid NOT NULL DEFAULT gen_random_uuid(),
    "previous_membership_holder_user_id" int4,
    CONSTRAINT "customer_loyalty_program_memberships_archive_pkey" PRIMARY KEY ("id")
);

CREATE TABLE "public"."posts" (
    "id" bigserial NOT NULL,
    "author_id" int4 NOT NULL,
    "title" text NOT NULL,
    "body" text,
    "published" bool NOT NULL DEFAULT false,
    CONSTRAINT "posts_pkey" PRIMARY KEY ("id")
);

CREATE TABLE "public"."users" (
    "id" serial NOT NULL,
    "email" varchar(255) NOT NULL,
    "display_name" text,
    "created_at" timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT "users_pkey" PRIMARY KEY ("id"),
    CONSTRAINT "users_email_key" UNIQUE ("email")
);

CREATE INDEX "posts_author_id_idx" ON "public"."posts" ("author_id");

ALTER TABLE "public"."customer_loyalty_program_memberships_archive" ADD CONSTRAINT "customer_loyalty_program_memb_previous_membership_holder_u_fkey" FOREIGN KEY ("previous_membership_holder_user_id") REFERENCES "public"."users" ("id");

ALTER TABLE "public"."posts" ADD CONSTRAINT "posts_author_id_fkey" FOREIGN KEY ("author_id") REFERENCES "public"."users" ("id") ON DELETE CASCADE;

