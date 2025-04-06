# VMOD reference

Add `import tinykvm;` at the top of your VCL to load the TinyKVM VMOD.

---
> `tinykvm.library(uri)`

- Fetch library of program definitions from the given URI.
- Libraries can contain many program definitions, but programs are only started on-demand.
- Must be called from vcl_init.

---
> `tinykvm.init_self_requests(unix_path, prefix, max_concurrent_requests)`

- Used by all self-requests.
- Set the location where Varnish (or another server) can be found.
- If unix_path is specified, it must be a Unix Domain Socket path accessible by Varnish.
  Otherwise, prefix must be accessible.
- The max concurrent self-requests parameter is a last resort to avoid loops.
- Must be called from vcl_init.

---
> `tinykvm.invalidate_programs(pattern)`

- Unloads programs matching the regex pattern.
- The next request to an unloaded program will cause it to be reloaded.
- If the old program has registered a live-update state serialization callback,
  the new program will be loaded immediately and *not* when the next request
  comes in, in order to facilitate a state transfer.

---
> `tinykvm.configure(program, json)`

- Provide a JSON configuration to override defaults to unstarted programs.
- The JSON configuration is the same as when configuring a program normally,
  but there are no mandatory fields.
- Must be called from vcl_init.

---
> `tinykvm.start(program, async = true, debug = false)`

- Start program during early Varnish startup.
- When async, program initialization will not delay Varnish from starting up.
  Requests to the program will always wait until it is fully initialized.
- When debug is enabled, immediately start remote GDB session.
- Must be called from vcl_init.

---
> `tinykvm.stats(pattern = "", indentation = -1)`

- Produces a JSON string with detailed statistics from each program matching the pattern.
- Unloaded programs will not produce statistics.
- When indentation is -1, minify the JSON.

---
> `tinykvm.chain(program, argument = "", config = "")`

- Queue this program up for execution in the exact order given.
- Returns true if the program was found.
- A program chain always end with a call to `tinykvm.program()`.
- Must be called from vcl_backend_fetch.

---
> `tinykvm.program(program, argument = "", config = "")`

- Returns a backend that can be used from VCL. It will call the given program to produce a response.
  The backend executes each program in the chain, and *then* runs the program assigned to the backend to produce a final response.
- Supports GET, POST and other HTTP requests, as well as streaming modes.
- All computation happens in between vcl_backend_fetch and vcl_backend_response.
- Must be called from vcl_backend_fetch.

---
> `tinykvm.to_string(program, argument = "", config = "", on_error = "")`

- Returns a string of the response produced by the given program.
- Supports GET, POST and other HTTP requests.
- If the program fails or returns an error, this function returns the on_error string instead.
- Works with chaining, and calls to_string() for the final string after processing chain.
- NOTE: Uses extra workspace for each call. See: man varnishd, workspace_backend.

---
> `tinykvm.synth(status, program, url = "", arg = "")`

- Directly delivers a synthetic response. If status is non-zero, final HTTP status will be overridden.
- Generates a synthetic response from the given program and arguments.
- If the synthetic response fails, the function returns 0.
- Works the same way as to_string(), and supports chaining.

---
> `tinykvm.live_update(program, key, max_size = 50MB)`

- Hotswap the current program with a new one from a POST request. If the program fails to
  load, the old program will be kept.
- The key is used to decide whether the poster is allowed to POST programs. It does not
  have to be used, but it has to match 'key' in the JSON configuration.
- If the old and new programs have registered live-update state serialization callbacks,
  they will be called on both the old and the new programs.
- Example:
```
	sub vcl_backend_fetch {
		if (bereq.method == "POST" && bereq.http.X-LiveUpdate) {
			set bereq.backend = tinykvm.live_update(
				bereq.http.Host, bereq.http.X-LiveUpdate, 100MB);
			return (fetch);
		}
	}
```

---
> `tinykvm.live_update_file(program, filename)`

- Hotswap the current program with a new one from the local filesystem.
- If the old and new programs have registered live-update state serialization callbacks,
  they will be called on both the old and the new programs.

