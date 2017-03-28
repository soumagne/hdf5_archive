#include "h5dsm_example.h"

static void
test_usage(const char *execname)
{
    printf("usage: %s <pool_uuid> <filename>\n", execname);
}

int
main(int argc, char *argv[])
{
    char *pool_grp = NULL;
    hid_t file = -1, fapl = -1, context = -1, dset = -1, space = -1;
    hsize_t dims[2] = {4, 6};
    int buf1[4][6], buf2[4][6];
    char name[64];
    int my_rank;
    int i, j;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    sprintf(name, "dset%d", my_rank);

    (void)daos_init();

    if(argc != 3) {
        test_usage(argv[0]);
        goto error;
    }

    /* Initialize buffer */
    for(i = 0; i < 4; i++) {
        for(j = 0; j < 6; j++) {
            buf1[i][j] = rand() % 10;
            buf2[i][j] = -1;
        }
    }

    /* Set up FAPL */
    if((fapl = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        ERROR;
    if(H5Pset_fapl_daos(fapl, MPI_COMM_WORLD, MPI_INFO_NULL, argv[1], pool_grp) < 0)
        ERROR;

    /* Create file */
    if((context = H5CXcreate()) < 0)
        ERROR;

    /* Create file */
    if((file = H5Fcreate_ff(argv[2], H5F_ACC_TRUNC, H5P_DEFAULT, fapl, context)) < 0)
        ERROR;

    /* Equivalent to
     * if(H5Pset_fcpl_async(fcpl) < 0)
     *     ERROR;
     * if((file = H5Fcreate(argv[2], H5F_ACC_TRUNC, fcpl, fapl)) < 0)
     *     ERROR;
     */

    /* Set up dataspace */
    if((space = H5Screate_simple(2, dims, NULL)) < 0)
        ERROR;

    /* Create dataset */
    if((dset = H5Dcreate(file, name, H5T_NATIVE_INT, space, H5P_DEFAULT,
        H5P_DEFAULT, H5P_DEFAULT)) < 0)
        ERROR;

    /* Fill and print buffer */
    printf("Writing data. Buffer is:\n");
    for(i = 0; i < 4; i++) {
        for(j = 0; j < 6; j++) {
            printf("%d ", buf1[i][j]);
        }
        printf("\n");
    }

    /* Write data */
    if(H5Dwrite(dset, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, buf1) < 0)
        ERROR;

    /* Write data */
    if(H5Fflush(file, H5F_SCOPE_GLOBAL) < 0)
        ERROR;

    /* Wait */
    if (H5CXwait(context, 10000, NULL) < 0)
        ERROR;

    /* Read data */
    if(H5Dread(dset, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, buf2) < 0)
        ERROR;

    /* Wait */
    if (H5CXwait(context, 10000, NULL) < 0)
        ERROR;

    /* Print buffer */
    printf("Successfully read data. Buffer is:\n");
    for(i = 0; i < 4; i++) {
        for(j = 0; j < 6; j++)
            printf("%d ", buf2[i][j]);
        printf("\n");
    }

    /* Close */
    if(H5Dclose(dset) < 0)
        ERROR;

    if(H5Sclose(space) < 0)
        ERROR;

    /* Close */
    if(H5Fclose(file) < 0)
        ERROR;

    if (H5CXwait(context, 10000, NULL) < 0)
        ERROR;
    if(H5CXclose(context) < 0)
        ERROR;
    if(H5Pclose(fapl) < 0)
        ERROR;

    printf("Success\n");

    (void)daos_fini();
    (void)MPI_Finalize();
    return 0;

error:
    H5E_BEGIN_TRY {
        H5Fclose(file);
        H5CXclose(context);
        H5Pclose(fapl);
    } H5E_END_TRY;

    (void)daos_fini();
    (void)MPI_Finalize();
    return 1;
}

