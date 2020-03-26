#
# varnish tests using CTest
#

# can be overridden with -DTESTS=xxx.vtc
# we can probably GLOB this, after all... why not?
set(TESTS
	a00000.vtc  b00041.vtc  c00061.vtc  e00003.vtc  m00031.vtc  P00003.vtc  r00941.vtc  r01510.vtc  r02036.vtc  t00001.vtc
	a00001.vtc  b00042.vtc  c00062.vtc  e00004.vtc  m00032.vtc  p00004.vtc  r00942.vtc  r01512.vtc  r02042.vtc  t00002.vtc
	a00002.vtc  b00043.vtc  c00063.vtc  e00005.vtc  m00033.vtc  P00004.vtc  r00948.vtc  r01518.vtc  r02069.vtc  t00003.vtc
	a00003.vtc  b00044.vtc  c00064.vtc  e00006.vtc  m00034.vtc  p00005.vtc  r00956.vtc  r01524.vtc  r02084.vtc  t00004.vtc
	a00004.vtc  b00045.vtc  c00065.vtc  e00007.vtc  m00035.vtc  P00005.vtc  r00961.vtc  r01532.vtc  r02105.vtc  t02000.vtc
	a00005.vtc  b00046.vtc  c00066.vtc  e00008.vtc  m00036.vtc  p00006.vtc  r00962.vtc  r01557.vtc  r02135.vtc  t02001.vtc
	a00006.vtc  b00047.vtc  c00067.vtc  e00009.vtc  m00037.vtc  p00007.vtc  r00963.vtc  r01562.vtc  r02142.vtc  t02002.vtc
	a00007.vtc  b00048.vtc  c00068.vtc  e00010.vtc  m00038.vtc  p00008.vtc  r00965.vtc  r01566.vtc  r02148.vtc  t02003.vtc
	a00008.vtc  b00049.vtc  c00069.vtc  e00011.vtc  m00039.vtc  p00009.vtc  r00972.vtc  r01569.vtc  r02157.vtc  t02004.vtc
	a00010.vtc  b00050.vtc  c00070.vtc  e00012.vtc  m00040.vtc  r00102.vtc  r00979.vtc  r01575.vtc  r02175.vtc  t02005.vtc
	a00011.vtc  b00051.vtc  c00071.vtc  e00013.vtc  m00041.vtc  r00251.vtc  r00980.vtc  r01577.vtc  r02177.vtc  t02006.vtc
	a00012.vtc  b00052.vtc  c00072.vtc  e00014.vtc  m00042.vtc  r00255.vtc  r00984.vtc  r01578.vtc  r02219.vtc  t02007.vtc
	a00013.vtc  b00053.vtc  c00073.vtc  e00015.vtc  m00043.vtc  r00262.vtc  r01002.vtc  r01581.vtc  r02233.vtc  t02008.vtc
	a00014.vtc  b00054.vtc  c00074.vtc  e00016.vtc  m00044.vtc  r00263.vtc  r01014.vtc  r01598.vtc  r02258.vtc  t02009.vtc
	a00015.vtc  b00055.vtc  c00075.vtc  e00017.vtc  m00045.vtc  r00292.vtc  r01029.vtc  r01602.vtc  r02262.vtc  t02010.vtc
	a00018.vtc  b00056.vtc  c00076.vtc  e00018.vtc  m00046.vtc  r00306.vtc  r01030.vtc  r01608.vtc  r02266.vtc  t02011.vtc
	a02000.vtc  b00057.vtc  c00077.vtc  e00019.vtc  m00047.vtc  r00310.vtc  r01036.vtc  r01612.vtc  r02270.vtc  t02012.vtc
	a02001.vtc  b00059.vtc  c00078.vtc  e00020.vtc  m00049.vtc  r00318.vtc  r01037.vtc  r01613.vtc  r02275.vtc  t02013.vtc
	a02002.vtc  b00060.vtc  c00079.vtc  e00021.vtc  m00050.vtc  r00325.vtc  r01038.vtc  r01624.vtc  r02291.vtc  t02014.vtc
	a02003.vtc  b00061.vtc  c00080.vtc  e00022.vtc  m01000.vtc  r00326.vtc  r01068.vtc  r01627.vtc  r02300.vtc  u00000.vtc
	a02004.vtc  b00062.vtc  c00081.vtc  e00023.vtc  m30000.vtc  r00345.vtc  r01073.vtc  r01637.vtc  r02305.vtc  u00003.vtc
	a02005.vtc  b00063.vtc  c00082.vtc  e00024.vtc  m30001.vtc  r00386.vtc  r01086.vtc  r01638.vtc  r02307.vtc  u00004.vtc
	a02006.vtc  b00064.vtc  c00083.vtc  e00025.vtc  m30002.vtc  r00387.vtc  r01092.vtc  r01641.vtc  r02310.vtc  u00005.vtc
	a02007.vtc  b00065.vtc  c00084.vtc  e00026.vtc  m30003.vtc  r00400.vtc  r01109.vtc  r01644.vtc  r02319.vtc  u00006.vtc
	a02008.vtc  b00066.vtc  c00085.vtc  e00027.vtc  m30004.vtc  r00409.vtc  r01113.vtc  r01646.vtc  r02321.vtc  u00007.vtc
	a02009.vtc  b00067.vtc  c00086.vtc  e00028.vtc  m30005.vtc  r00411.vtc  r01120.vtc  r01648.vtc  r02325.vtc  u00009.vtc
	a02010.vtc  c00001.vtc  c00087.vtc  e00030.vtc  m30006.vtc  r00412.vtc  r01123.vtc  r01650.vtc  r02339.vtc  u00010.vtc
	a02011.vtc  c00002.vtc  c00088.vtc  e00031.vtc  m30007.vtc  r00416.vtc  r01134.vtc  r01660.vtc  r02342.vtc  u00011.vtc
	a02012.vtc  c00003.vtc  c00089.vtc  f00001.vtc  m30008.vtc  r00425.vtc  r01140.vtc  r01662.vtc  r02351.vtc  u00012.vtc
	a02013.vtc  c00004.vtc  c00090.vtc  f00004.vtc  m30009.vtc  r00427.vtc  r01144.vtc  r01665.vtc  r02367.vtc  u00013.vtc
	a02014.vtc  c00005.vtc  c00091.vtc  f10003.vtc  m30010.vtc  r00433.vtc  r01145.vtc  r01672.vtc  r02372.vtc  u00014.vtc
	a02015.vtc  c00006.vtc  c00092.vtc  g00000.vtc  m30011.vtc  r00445.vtc  r01156.vtc  r01684.vtc  r02387.vtc  v00000.vtc
	a02016.vtc  c00007.vtc  c00093.vtc  g00001.vtc  m30012.vtc  r00466.vtc  r01157.vtc  r01688.vtc  r02406.vtc  v00001.vtc
	a02017.vtc  c00008.vtc  c00094.vtc  g00002.vtc  m30013.vtc  r00476.vtc  r01164.vtc  r01691.vtc  r02413.vtc  v00002.vtc
	a02018.vtc  c00009.vtc  c00095.vtc  g00003.vtc  m30014.vtc  r00494.vtc  r01168.vtc  r01693.vtc  r02418.vtc  v00003.vtc
	a02019.vtc  c00010.vtc  c00097.vtc  g00004.vtc  m30015.vtc  r00495.vtc  r01169.vtc  r01729.vtc  r02422.vtc  v00005.vtc
	a02020.vtc  c00011.vtc  c00098.vtc  g00005.vtc  m30016.vtc  r00498.vtc  r01175.vtc  r01730.vtc  r02429.vtc  v00006.vtc
	a02021.vtc  c00012.vtc  c00099.vtc  g00006.vtc  m30017.vtc  r00502.vtc  r01176.vtc  r01737.vtc  r02432.vtc  v00008.vtc
	a02022.vtc  c00013.vtc  c00100.vtc  g00007.vtc  m30018.vtc  r00506.vtc  r01184.vtc  r01739.vtc  r02433.vtc  v00010.vtc
	a02023.vtc  c00014.vtc  d00000.vtc  h00001.vtc  m30019.vtc  r00524.vtc  r01211.vtc  r01746.vtc  r02451.vtc  v00011.vtc
	a02024.vtc  c00015.vtc  d00001.vtc  h00002.vtc  m30020.vtc  r00545.vtc  r01212.vtc  r01755.vtc  r02488.vtc  v00012.vtc
	b00000.vtc  c00016.vtc  d00002.vtc  h00003.vtc  m30021.vtc  r00549.vtc  r01218.vtc  r01761.vtc  r02494.vtc  v00013.vtc
	b00001.vtc  c00017.vtc  d00003.vtc  h00004.vtc  m30022.vtc  r00558.vtc  r01225.vtc  r01762.vtc  r02527.vtc  v00014.vtc
	b00002.vtc  c00018.vtc  d00004.vtc  h00005.vtc  m30023.vtc  r00561.vtc  r01255.vtc  r01764.vtc  r02530.vtc  v00015.vtc
	b00003.vtc  c00019.vtc  d00005.vtc  j00000.vtc  m30025.vtc  r00590.vtc  r01266.vtc  r01765.vtc  r02539.vtc  v00016.vtc
	b00004.vtc  c00020.vtc  d00006.vtc  j00001.vtc  m30026.vtc  r00612.vtc  r01274.vtc  r01768.vtc  r02554.vtc  v00017.vtc
	b00005.vtc  c00021.vtc  d00007.vtc  j00003.vtc  m30027.vtc  r00641.vtc  r01275.vtc  r01772.vtc  r02555.vtc  v00018.vtc
	b00006.vtc  c00022.vtc  d00008.vtc  j00004.vtc  m30028.vtc  r00655.vtc  r01284.vtc  r01775.vtc  r02589.vtc  v00019.vtc
	b00007.vtc  c00023.vtc  d00009.vtc  l00000.vtc  m30029.vtc  r00667.vtc  r01287.vtc  r01777.vtc  r02617.vtc  v00020.vtc
	b00008.vtc  c00024.vtc  d00010.vtc  l00001.vtc  m30030.vtc  r00679.vtc  r01296.vtc  r01781.vtc  r02618.vtc  v00021.vtc
	b00009.vtc  c00025.vtc  d00011.vtc  l00002.vtc  m30031.vtc  r00686.vtc  r01312.vtc  r01783.vtc  r02633.vtc  v00024.vtc
	b00010.vtc  c00026.vtc  d00012.vtc  l00003.vtc  m30032.vtc  r00694.vtc  r01320.vtc  r01801.vtc  r02645.vtc  v00025.vtc
	b00011.vtc  c00027.vtc  d00013.vtc  l00004.vtc  m30033.vtc  r00700.vtc  r01332.vtc  r01804.vtc  r02646.vtc  v00027.vtc
	b00012.vtc  c00028.vtc  d00014.vtc  l00005.vtc  m30034.vtc  r00702.vtc  r01333.vtc  r01806.vtc  r02647.vtc  v00031.vtc
	b00013.vtc  c00031.vtc  d00015.vtc  m00000.vtc  m30035.vtc  r00704.vtc  r01335.vtc  r01807.vtc  r02649.vtc  v00032.vtc
	b00014.vtc  c00034.vtc  d00016.vtc  m00001.vtc  m30036.vtc  r00722.vtc  r01337.vtc  r01810.vtc  r02679.vtc  v00033.vtc
	b00015.vtc  c00035.vtc  d00017.vtc  m00002.vtc  m30037.vtc  r00733.vtc  r01349.vtc  r01818.vtc  r02686.vtc  v00034.vtc
	b00016.vtc  c00036.vtc  d00018.vtc  m00003.vtc  m30038.vtc  r00742.vtc  r01350.vtc  r01821.vtc  r02690.vtc  v00037.vtc
	b00017.vtc  c00037.vtc  d00019.vtc  m00004.vtc  m30039.vtc  r00763.vtc  r01355.vtc  r01826.vtc  r02700.vtc  v00038.vtc
	b00018.vtc  c00038.vtc  d00020.vtc  m00005.vtc  m30040.vtc  r00769.vtc  r01356.vtc  r01834.vtc  r02702.vtc  v00039.vtc
	b00019.vtc  c00039.vtc  d00021.vtc  m00006.vtc  m30041.vtc  r00776.vtc  r01367.vtc  r01837.vtc  r02705.vtc  v00040.vtc
	b00020.vtc  c00040.vtc  d00022.vtc  m00007.vtc  m30042.vtc  r00781.vtc  r01391.vtc  r01838.vtc  r02763.vtc  v00041.vtc
	b00021.vtc  c00041.vtc  d00023.vtc  m00008.vtc  m30043.vtc  r00789.vtc  r01395.vtc  r01843.vtc  r02775.vtc  v00042.vtc
	b00022.vtc  c00043.vtc  d00024.vtc  m00011.vtc  m30044.vtc  r00795.vtc  r01398.vtc  r01847.vtc  r02831.vtc  v00043.vtc
	b00023.vtc  c00044.vtc  d00025.vtc  m00012.vtc  m30045.vtc  r00801.vtc  r01399.vtc  r01856.vtc  r02839.vtc  v00045.vtc
	b00024.vtc  c00045.vtc  d00026.vtc  m00013.vtc  m30046.vtc  r00803.vtc  r01401.vtc  r01857.vtc  r02880.vtc  v00046.vtc
	b00025.vtc  c00046.vtc  d00027.vtc  m00014.vtc  m30047.vtc  r00806.vtc  r01404.vtc  r01858.vtc  r02976.vtc  v00047.vtc
	b00026.vtc  c00047.vtc  d00028.vtc  m00015.vtc  m30048.vtc  r00832.vtc  r01406.vtc  r01878.vtc  r10444.vtc  v00049.vtc
	b00027.vtc  c00048.vtc  d00029.vtc  m00017.vtc  m30049.vtc  r00861.vtc  r01417.vtc  r01879.vtc  r10456.vtc  v00050.vtc
	b00028.vtc  c00049.vtc  d00030.vtc  m00018.vtc  m30050.vtc  r00873.vtc  r01419.vtc  r01881.vtc  r10471.vtc  v00051.vtc
	b00029.vtc  c00050.vtc  d00031.vtc  m00019.vtc  o00000.vtc  r00878.vtc  r01441.vtc  r01890.vtc  s00000.vtc  v00052.vtc
	b00030.vtc  c00051.vtc  d00032.vtc  m00020.vtc  o00001.vtc  r00894.vtc  r01442.vtc  r01914.vtc  s00001.vtc  v00054.vtc
	b00031.vtc  c00052.vtc  d00033.vtc  m00021.vtc  o00002.vtc  r00896.vtc  r01468.vtc  r01918.vtc  s00002.vtc  v00055.vtc
	b00032.vtc  c00053.vtc  d00034.vtc  m00022.vtc  o00003.vtc  r00902.vtc  r01478.vtc  r01924.vtc  s00003.vtc  v00056.vtc
	b00033.vtc  c00054.vtc  d00035.vtc  m00023.vtc  o00004.vtc  r00911.vtc  r01485.vtc  r01927.vtc  s00004.vtc  v00057.vtc
	b00034.vtc  c00055.vtc  d00036.vtc  m00024.vtc  o00005.vtc  r00913.vtc  r01493.vtc  r01941.vtc  s00005.vtc  v00058.vtc
	b00036.vtc  c00056.vtc  d00037.vtc  m00025.vtc  p00000.vtc  r00915.vtc  r01494.vtc  r01953.vtc  s00006.vtc  v00060.vtc
	b00037.vtc  c00057.vtc  d00038.vtc  m00026.vtc  P00001.vtc  r00916.vtc  r01498.vtc  r01955.vtc  s00007.vtc  v00062.vtc
	b00038.vtc  c00058.vtc  e00000.vtc  m00027.vtc  p00002.vtc  r00917.vtc  r01499.vtc  r01956.vtc  s00008.vtc
	b00039.vtc  c00059.vtc  e00001.vtc  m00028.vtc  P00002.vtc  r00921.vtc  r01501.vtc  r01990.vtc  s00009.vtc
	b00040.vtc  c00060.vtc  e00002.vtc  m00029.vtc  p00003.vtc  r00940.vtc  r01504.vtc  r02035.vtc  t00000.vtc
)

set(TESTS_PATH  ${VARNISH_PATH}/bin/varnishtest/tests)
set(VARNISHD    ${CMAKE_BINARY_DIR}/varnishd)
set(VARNISHTEST ${CMAKE_BINARY_DIR}/varnishtest)

file(GLOB children ${VARNISH_PATH}/lib/libvmod_*)
foreach(child ${children})
	if(IS_DIRECTORY ${child})
		#message(STATUS "VMOD: ${child}")
		set(VMOD_PATHS "${VMOD_PATHS}${child}/.libs:")
	endif()
endforeach()

enable_testing()
foreach (TEST ${TESTS})
	get_filename_component(TNAME ${TEST} NAME_WE)
	add_test(
		NAME tests_${TNAME}
		COMMAND ${VARNISHTEST} "-Dvarnishd=${VARNISHD}" -p "vmod_path=\"${VMOD_PATHS}\"" ${TEST}
		WORKING_DIRECTORY ${TESTS_PATH}
	)
endforeach()
