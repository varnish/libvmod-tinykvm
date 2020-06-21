static inline struct vmod_priv *
vmod_util_get_priv_task(struct req *req, struct busyobj *bo, const void *id)
{
	struct vrt_ctx ctx;
	struct vmod_priv *task_priv;

	INIT_OBJ(&ctx, VRT_CTX_MAGIC);

	if (req != NULL) {
		CHECK_OBJ(req, REQ_MAGIC);
		ctx.vsl = req->vsl;
		ctx.ws = req->ws;
		ctx.req = req;
	} else if (bo != NULL) {
		CHECK_OBJ(bo, BUSYOBJ_MAGIC);
		ctx.vsl = bo->vsl;
		ctx.ws = bo->ws;
		ctx.bo = bo;
	} else {
		WRONG("vmod_util_get_priv_task needs a valid req or bo");
	}

	task_priv = VRT_priv_task(&ctx, id);
	return (task_priv);
}
