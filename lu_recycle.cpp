#include <iostream>
#include <mpi.h>
#include <sys/time.h>
#include <pmmintrin.h>
#include <omp.h>

using namespace std;

static const int N = 1000;
static const int thread_count = 4;

float arr[N][N];
float A[N][N];


void init_A(float arr[][N])
{
    for (int i = 0; i < N; i++)
    {
        for (int j = 0; j < N; j++)
        {
            arr[i][j] = 0;
        }
        arr[i][i] = 1.0;
        for (int j = i + 1; j < N; j++)
            arr[i][j] = rand() % 100;
    }

    for (int i = 0; i < N; i++)
    {
        int k1 = rand() % N;
        int k2 = rand() % N;
        for (int j = 0; j < N; j++)
        {
            arr[i][j] += arr[0][j];
            arr[k1][j] += arr[k2][j];
        }
    }
}

void reset_A(float A[][N], float arr[][N])
{
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            A[i][j] = arr[i][j];
}


void print_A(float A[][N])
{
    for (int i = 0; i < N; i++)
    {
        for (int j = 0; j < N; j++)
            cout << A[i][j] << " ";
        cout << endl;
    }
    cout << endl;
}



void LU(float A[][N], int rank, int num_proc)
{
    for (int k = 0; k < N; k++)
    {
        //��ǰ�����Լ����̵����񡪡�������ȥ
        if (int(k % num_proc) == rank)
        {
            for (int j = k + 1; j < N; j++)
                A[k][j] = A[k][j] / A[k][k];
            A[k][k] = 1.0;
            //��ȥ�����������̷�����Ϣ
            for (int p = 0; p < num_proc; p++)
                if (p != rank)
                    MPI_Send(&A[k], N, MPI_FLOAT, p, 2, MPI_COMM_WORLD);
        }
        //��ǰ�в����Լ����̵����񡪡�������Ϣ
        else
        {
            MPI_Recv(&A[k], N, MPI_FLOAT, int(k % num_proc), 2,
                MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
        for (int i = k + 1; i < N; i++)
        {
            if (int(i % num_proc) == rank)
            {
                for (int j = k + 1; j < N; j++)
                    A[i][j] = A[i][j] - A[i][k] * A[k][j];
                A[i][k] = 0.0;
            }
        }
    }
}

void f_mpi()
{
    timeval t_start;
    timeval t_end;

    int num_proc;//������
    int rank;//ʶ����ý��̵�rank��ֵ��0~size-1

    MPI_Comm_size(MPI_COMM_WORLD, &num_proc);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    //0�Ž��̡������񻮷�
    if (rank == 0)
    {
        reset_A(A, arr);
        gettimeofday(&t_start, NULL);
        //���񻮷�
        for (int i = 0; i < N; i++)
        {
            int flag = i % num_proc;
            if (flag == rank)
                continue;
            else
                MPI_Send(&A[i], N, MPI_FLOAT, flag, 0, MPI_COMM_WORLD);
        }
        LU(A, rank, num_proc);
        //������0�Ž����Լ��������������������̴���֮��Ľ��
        for (int i = 0; i < N; i++)
        {
            int flag = i % num_proc;
            if (flag == rank)
                continue;
            else
                MPI_Recv(&A[i], N, MPI_FLOAT, flag, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
        gettimeofday(&t_end, NULL);
        cout << "Recycle MPI LU time cost: "
            << 1000 * (t_end.tv_sec - t_start.tv_sec) +
            0.001 * (t_end.tv_usec - t_start.tv_usec) << "ms" << endl;
        //print_A(A);
    }
    //��������
    else
    {
        //��0�Ž����Ƚ�������
        for (int i = rank; i < N; i += num_proc)
        {
            MPI_Recv(&A[i], N, MPI_FLOAT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
        //ִ������
        LU(A, rank, num_proc);
        //��0�Ž����������֮�󣬽�������ص�0�Ž���
        for (int i = rank; i < N; i += num_proc)
        {
            MPI_Send(&A[i], N, MPI_FLOAT, 0, 1, MPI_COMM_WORLD);
        }
    }
}

void LU_opt(float A[][N], int rank, int num_proc)
{
    __m128 t1, t2, t3;
#pragma omp parallel num_threads(thread_count)
    for (int k = 0; k < N; k++)
    {
        if (int(k % num_proc) == rank)
        {
            float temp1[4] = { A[k][k], A[k][k], A[k][k], A[k][k] };
            t1 = _mm_loadu_ps(temp1);
            int j = k + 1;
#pragma omp for schedule(dynamic, 20)
            for (j; j < N - 3; j += 4)
            {
                t2 = _mm_loadu_ps(A[k] + j);
                t3 = _mm_div_ps(t2, t1);
                _mm_storeu_ps(A[k] + j, t3);
            }
#pragma omp for schedule(dynamic, 20)
            for (j; j < N; j++)
            {
                A[k][j] = A[k][j] / A[k][k];
            }
            A[k][k] = 1.0;

            for (int p = 0; p < num_proc; p++)
                if (p != rank)
                    MPI_Send(&A[k], N, MPI_FLOAT, p, 2, MPI_COMM_WORLD);
        }
        else
        {
            MPI_Recv(&A[k], N, MPI_FLOAT, int(k % num_proc), 2,
                MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
        for (int i = k + 1; i < N; i++)
        {
            if (int(i % num_proc) == rank)
            {
                float temp2[4] = { A[i][k], A[i][k], A[i][k], A[i][k] };
                t1 = _mm_loadu_ps(temp2);
                int j = k + 1;
#pragma omp for schedule(dynamic, 20)
                for (j; j <= N - 3; j += 4)
                {
                    t2 = _mm_loadu_ps(A[i] + j);
                    t3 = _mm_loadu_ps(A[k] + j);
                    t3 = _mm_mul_ps(t1, t3);
                    t2 = _mm_sub_ps(t2, t3);
                    _mm_storeu_ps(A[i] + j, t2);
                }
#pragma omp for schedule(dynamic, 20)
                for (j; j < N; j++)
                    A[i][j] = A[i][j] - A[i][k] * A[k][j];
                A[i][k] = 0;
            }
        }
    }
}

void f_mpi_opt()
{
    timeval t_start;
    timeval t_end;

    int num_proc;
    int rank;

    MPI_Comm_size(MPI_COMM_WORLD, &num_proc);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == 0)
    {
        reset_A(A, arr);
        gettimeofday(&t_start, NULL);
        for (int i = 0; i < N; i++)
        {
            int flag = i % num_proc;
            if (flag == rank)
                continue;
            else
                MPI_Send(&A[i], N, MPI_FLOAT, flag, 0, MPI_COMM_WORLD);
        }
        LU_opt(A, rank, num_proc);
        for (int i = 0; i < N; i++)
        {
            int flag = i % num_proc;
            if (flag == rank)
                continue;
            else
                MPI_Recv(&A[i], N, MPI_FLOAT, flag, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
        gettimeofday(&t_end, NULL);
        cout << "Recycle MPI LU with SSE and OpenMP time cost: "
            << 1000 * (t_end.tv_sec - t_start.tv_sec) +
            0.001 * (t_end.tv_usec - t_start.tv_usec) << "ms" << endl;
        //print_A(A);
    }
    else
    {
        for (int i = rank; i < N; i += num_proc)
        {
            MPI_Recv(&A[i], N, MPI_FLOAT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
        LU_opt(A, rank, num_proc);
        for (int i = rank; i < N; i += num_proc)
        {
            MPI_Send(&A[i], N, MPI_FLOAT, 0, 1, MPI_COMM_WORLD);
        }
    }
}


int main()
{
    init_A(arr);

    MPI_Init(NULL, NULL);

    f_mpi();
    f_mpi_opt();
    MPI_Finalize();



}














void f_ordinary()
{
    bool sign;
    do
    {
        //������ش�����Ԫ��------------------------------------------------------
        //---------------------------begin-------------------------------------

        int i;
        for (i = lieNum - 1; i - 8 >= -1; i -= 8)
        {
            //ÿ�ִ���8����Ԫ�ӣ���Χ�������� i-7 �� i
            for (int j = 0; j < pasNum; j++)
            {
                //������Ԫ����û�������ڴ˷�Χ�ڵ�
                while (Pas[j][Num] <= i && Pas[j][Num] >= i - 7)
                {
                    int index = Pas[j][Num];

                    if (Act[index][Num] == 1)//��Ԫ�Ӳ�Ϊ��
                    {
                        //Pas[j][]��Act[��Pas[j][x]��][]�����
                        for (int k = 0; k < Num; k++)
                        {
                            Pas[j][k] = Pas[j][k] ^ Act[index][k];
                        }




                        //����Pas[j][18]�������ֵ
                        //�������֮������������������浽Pas[j][18]�������ڷ�Χ������whileѭ��
                        //�����֮��Pas[j][ ]������
                        int num = 0, S_num = 0;
                        for (num = 0; num < Num; num++)
                        {
                            if (Pas[j][num] != 0)
                            {
                                unsigned int temp = Pas[j][num];
                                while (temp != 0)
                                {
                                    temp = temp >> 1;
                                    S_num++;
                                }
                                S_num += num * 32;
                                break;
                            }
                        }
                        Pas[j][Num] = S_num - 1;
                    }
                    else//��Ԫ��Ϊ��
                    {
                        break;
                    }
                }
            }
        }

        for (i = i + 8; i >= 0; i--)
        {
            //ÿ�ִ���1����Ԫ�ӣ���Χ���������i

            for (int j = 0; j < pasNum; j++)
            {
                //��53������Ԫ����û���������i��
                while (Pas[j][Num] == i)
                {
                    if (Act[i][Num] == 1)//��Ԫ�Ӳ�Ϊ��
                    {
                        //Pas[j][]��Act[i][]�����
                        for (int k = 0; k < Num; k++)
                        {
                            Pas[j][k] = Pas[j][k] ^ Act[i][k];
                        }

                        //����Pas[j][18]�������ֵ
                        //�������֮������������������浽Pas[j][18]�������ڷ�Χ������whileѭ��
                        //�����֮��Pas[j][ ]������
                        int num = 0, S_num = 0;
                        for (num = 0; num < Num; num++)
                        {
                            if (Pas[j][num] != 0)
                            {
                                unsigned int temp = Pas[j][num];
                                while (temp != 0)
                                {
                                    temp = temp >> 1;
                                    S_num++;
                                }
                                S_num += num * 32;
                                break;
                            }
                        }
                        Pas[j][Num] = S_num - 1;

                    }
                    else//��Ԫ��Ϊ��
                    {
                        break;
                    }
                }
            }
        }

        //----------------------------------end--------------------------------
        //������ش�����Ԫ��--------------------------------------------------------



        //������Ԫ�ӣ�Ȼ���ж��Ƿ����
        sign = false;
        for (int i = 0; i < pasNum; i++)
        {
            //�ҵ���i������Ԫ�е�����
            int temp = Pas[i][Num];
            if (temp == -1)
            {
                //˵�����Ѿ�������Ϊ��Ԫ����
                continue;
            }

            //����������Ӧ����Ԫ���ǲ���Ϊ�գ���Ϊ�գ�����
            if (Act[temp][Num] == 0)
            {
                //������Ԫ��
                for (int k = 0; k < Num; k++)
                    Act[temp][k] = Pas[i][k];
                //������Ԫ������
                Pas[i][Num] = -1;
                //��־bool��Ϊtrue��˵�����ֻ������
                sign = true;
            }
        }

    } while (sign == true);

}