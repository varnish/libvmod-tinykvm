extern ssize_t kvm_backend_streaming_delivery(
	struct backend_result *result, void* dst, ssize_t max_len, ssize_t written);

static enum vfp_status v_matchproto_(vfp_pull_f)
kvmfp_streaming_pull(struct vfp_ctx *vc, struct vfp_entry *vfe, void *p, ssize_t *lp)
{
	/* The resulting response of a backend request. */
	struct backend_result *result = (struct backend_result *)vfe->priv1;
    result->stream_vsl = vc->resp->vsl;

	/* Go through streamed responses and write it to storage. */
	ssize_t max = *lp;
	ssize_t written = 0;

	while (1) {
        ssize_t len = kvm_backend_streaming_delivery(result, p, max, vfe->priv2);
        /* Give up if no progress was made. */
        if (len <= 0) {
            return (VFP_ERROR);
        }
		written += len;
		p = (void *) ((char *)p + len);
        vfe->priv2 += len;

		/* End when we reached content length. */
		if (vfe->priv2 == (intptr_t)result->content_length) {
            *lp = written;
            return (VFP_END);
		}
		/* Return later if there's more, and we can't send more */
		max -= len;
		if (max == 0) {
			*lp = written;
			return (VFP_OK);
		}
	}
}

static const struct vfp kvm_streaming_fetch_processor = {
    .name = "kvm_backend",
    .pull = kvmfp_streaming_pull,
};
