set (LIBPATH ${VARNISH_PATH}/lib)

## MSE library ##
set(MSE_PATH ${LIBPATH}/libmse)
set(MSE_SOURCES
	${MSE_PATH}/mse_lib_jrnfile.c
	${MSE_PATH}/mse_lib_banlist.c
	${MSE_PATH}/mse_lib_db.c
	${MSE_PATH}/mse_lib_env.c
	${MSE_PATH}/mse_lib_env_db.c
	${MSE_PATH}/mse_lib_env_helpers.c
	${MSE_PATH}/mse_lib_env_halfopen.c
	${MSE_PATH}/mse_lib_config.c
	${MSE_PATH}/mse_lib_dbro.c
	${MSE_PATH}/mse_lib_journal.c
	${MSE_PATH}/mse_lib_processing.c
	#${MSE_PATH}/journal_test.c
	#${MSE_PATH}/mse_lib_config_test.c
	#${MSE_PATH}/mse_lib_dbtest.c
	#${MSE_PATH}/mse_mtq_test.c
)
add_library(mse ${MSE_SOURCES})
target_include_directories(mse PRIVATE ${MSE_PATH})
target_include_directories(mse PRIVATE ${LIBPATH}/libvgz)
target_include_directories(mse PRIVATE ${LIBPATH}/liblmdb/LMDB_0.9.23)
target_include_directories(mse PRIVATE ${VARNISH_PATH}/include)
target_include_directories(mse PRIVATE ${CONFIG_PATH})
# we need to link with libconfig
target_link_libraries(mse config)
if (SANITIZE)
	target_compile_options(mse PRIVATE "${SANITIZE_FLAGS}")
endif()
