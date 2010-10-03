/*
 * handrecog_lucykila.c - my first attempt to write my own online handwrite recognition engine
 *
 *  Created on: 2010-2-7
 *      Author: cai
 */

#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <math.h>
#include <string.h>
#include <glib.h>

#include "engine.h"
#include "handrecog.h"
#include "global_var.h"

typedef struct _MATCHED MATCHED;
typedef struct _IbusHandwriteRecogLucyKila IbusHandwriteRecogLucyKila;
typedef struct _IbusHandwriteRecogLucyKilaClass IbusHandwriteRecogLucyKilaClass;

GType ibus_handwrite_recog_lucykila_get_type(void);


#define IBUS_HANDWRITE_RECOG_LUCYKILA_GET_CLASS(obj) \
		G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_IBUS_HANDWRITE_RECOG_LUCYKILA, IbusHandwriteRecogLucyKilaClass)
#define IBUS_HANDWRITE_RECOG_LUCYKILA(obj) \
		G_TYPE_CHECK_INSTANCE_CAST(obj,G_TYPE_IBUS_HANDWRITE_RECOG_LUCYKILA,IbusHandwriteRecogLucyKila)

struct _MATCHED{
  char  code[64-16];
  char  hanzi[16];
};


//隐藏层
struct ANN_hide{
//	struct ANN_input (inputs)[9];
//	gfloat	value;
	gfloat	weights[9]; //每个接受8个输入，外加一个 bias
};

//输出层
struct ANN_output{
	gfloat	weights[6]; //五个隐藏节点，一个  bias
	gint	activation;
};

struct _IbusHandwriteRecogLucyKila
{
	IbusHandwriteRecog parent;
	GString * input; // 由笔画构成的，用来查笔画表的字符串
	void * start_ptr; //指向表的地址
	size_t items_count; //表项数
	size_t table_size; // 表大小
	size_t maped_size; // 分配的内存大小
	struct ANN_hide		annhide[5]; //五个hide节点
	struct ANN_output	annoutput[3]; //三个输出 。能有 2^3 种变化，识别8个笔画
};

struct _IbusHandwriteRecogLucyKilaClass
{
	IbusHandwriteRecogClass parent;
	void (* parentdestroy)(GObject *object);
};

static void ibus_handwrite_recog_lucykila_init(IbusHandwriteRecogLucyKila*obj);
static void ibus_handwrite_recog_lucykila_class_init(
		IbusHandwriteRecogLucyKilaClass* klass);

static char * nextline(char * ptr);
static gint mysort(gconstpointer a, gconstpointer b);

static int lucykila_open_table(IbusHandwriteRecogLucyKila*obj);

static void ibus_handwrite_recog_lucykila_train(IbusHandwriteRecogLucyKila * obj, gint inputs[5][8],gint output[5][3])
{
	//五组输入，五组输出

}

static inline gint calc_direction(GdkPoint p1, GdkPoint p2)
{
	double x,y;
	int		ix,iy;
	x = p2.x - p1.x;
	y = p2.y - p1.y;

	if( fabs(x) >fabs(y))
	{
		x *= fabs(1/x);
		y *= fabs(1/x);
	}else if(fabs(x)<fabs(y))
	{
		x *= fabs(1/y);
		y *= fabs(1/y);
	}else if(fabs(x)==fabs(y))
	{
		x *= fabs(1/x);
		y *= fabs(1/y);
	}

	ix = x + 0.5;
	iy = y + 0.5;

	if( x==1 && y == 0)
		return 0;
	else if(x==1 && y==1)
		return 1;
	else if(x==0 && y==1)
		return 2;
	else if(x==-1 && y==1)
		return 3;
	else if(x==-1 && y==0)
		return 4;
	else if(x==-1 && y==-1)
		return 5;
	else if(x==0 && y==-1)
		return 6;
	else if(x==1 && y==-1)
		return 7;
	else
		((int * )0)[0] = 0;
}

static gdouble calc_distence(GdkPoint p1, GdkPoint p2)
{
	int		x,y;
	x = p2.x - p1.x;
	y = p2.y - p1.y;

	return sqrt(x*x + y*y);
}

static inline GdkPoint make_midle_point(GdkPoint *p1, GdkPoint *p2)
{
	GdkPoint ret;
	ret.x = (p1->x + p2->x )/2;
	ret.y = (p1->y + p2->y )/2;

	return ret;
}

static void ibus_handwrite_recog_stroke_normolize(GdkPoint* points, gint num,gint output[8])
{
	int i;

	//9个点转化为8个方向
	if(G_UNLIKELY(num == 9))
	{
		int i;

		for(i=0;i<8;i++)
		{
			output[i] = calc_direction(points[i],points[i+1]);
		}
	}else if (G_LIKELY(num >9))
	{
		//去掉一个点
		//将距离最小的两个点减去，取其中的中间点
		GdkPoint* ptr = points;
		gdouble	 distance = 50000.0f;

		for (i = 0; i < num - 1; i++)
		{
			gdouble distance2 = calc_distence(points[i],points[i+1]);
			if(distance2 < distance)
			{
				distance = distance2;
				ptr = &points[i];
			}
		}

		if(ptr == points)
		{
			memcpy(points+1,points+2,sizeof(*ptr)*(num-2));
		}else
		{
			*ptr = make_midle_point(ptr,ptr+1);
			if((ptr - points)<2)
				memcpy(ptr+1,ptr+2,sizeof(*ptr)*( (ptr - points) -2));
			else
				ptr[1] = ptr[2];
		}

		return ibus_handwrite_recog_stroke_normolize(points,num-1,output);
	}else //都不够9个点？？？？？就是点咯
	{
		//这样保证被识别为 h
		memset(output,0,8);
	}

}


static gint	ibus_handwrite_recog_lucykila_stroke_ann_match(IbusHandwriteRecogLucyKila * obj,gint input[8])
{
	int i,j;
	//最后一个为 bias
	gfloat midle_layer[6]={0.0,0.0,0.0,0.0,0.0,-1.0};
	gint out_layer[3]={0,0,0};

	for(i = 0; i < 5 ; i++)
	{
		for(j=0;j<8;j++)
		{
			midle_layer[i]+=input[j]*obj->annhide[i].weights[j];
		}
		midle_layer[i]+= obj->annhide[i].weights[8];
	}

	for(i=0;i<3;i++)
	{
		obj->annoutput[i].activation = 0.0f;
		for(j=0;j<6;j++)
		{
			obj->annoutput[i].activation +=obj->annoutput[i].weights[j] * midle_layer[j];
		}

		out_layer[i] = obj->annoutput[i].activation>=0;
	}

	return ((out_layer[0] &1 ) << 0) |((out_layer[1] &1 ) << 1)|((out_layer[2] &1 ) << 2);
}

static void ibus_handwrite_recog_change_stroke(IbusHandwriteRecog* obj)
{
	IbusHandwriteRecogLucyKila * me;
	gint i,input[8];

	me = IBUS_HANDWRITE_RECOG_LUCYKILA(obj);

	if(obj->strokes->len == 0)
	{
		me->input= g_string_truncate(me->input,0);
		return ;
	}

	LineStroke laststroke = g_array_index(obj->strokes,LineStroke,obj->strokes->len-1);

	ibus_handwrite_recog_stroke_normolize(laststroke.points,laststroke.segments,input);

	gchar resultable[8]={
			'h','s','p','n','z','z','z','z'
	};

	me->input = g_string_append_c(me->input,resultable[ibus_handwrite_recog_lucykila_stroke_ann_match(me,input)]);
}


static gboolean ibus_handwrite_recog_lucykila_domatch(IbusHandwriteRecog*obj,int want)
{
	IbusHandwriteRecogLucyKila * me;
	MATCHED mt;
	char * ptr, *start_ptr, *p;
	int i, size = 0;

	me = IBUS_HANDWRITE_RECOG_LUCYKILA(obj);

	if(me->input->len == 0)
		return 0;

	GArray * result  = g_array_new(TRUE,TRUE,sizeof(MATCHED));
		puts(__func__);

	for (i = 0 , ptr = me->start_ptr ; i < me->items_count ; ++i , ptr+=64)
	{
		if (memcmp(ptr, me->input->str, me->input->len) == 0)
		{
			memset(&mt, 0, 64);
			p = ptr;
			while (*p != ' ' && *p != '\t')
				++p;
			memcpy(mt.code, ptr, p - ptr);
			while (*p == ' ' || *p == '\t')
				++p;
			strcpy(mt.hanzi, p);
			result = g_array_append_vals(result, &mt , 1 );
			size++;
		}
	}

	puts(__func__);

	//调节顺序
	g_array_sort(result, mysort);

	//载入 matched
	MatchedChar mc;

	obj->matched = g_array_set_size(obj->matched,0);

	for( i =0; i < size ;++i)
	{
		mt = g_array_index(result,MATCHED,i);

		strcpy(mc.chr , mt.hanzi);
		obj->matched = g_array_append_val(obj->matched,mc);
	}

	g_array_free(result,TRUE);

	return size;
}

static void ibus_handwrite_recog_lucykila_init(IbusHandwriteRecogLucyKila*obj)
{
	int i;
	obj->input = g_string_new("");
	obj->start_ptr; //指向表的地址
	obj->items_count = 0; //表项数
	obj->table_size = 0; // 表大小
	obj->maped_size = 0; // 分配的内存大小

	gfloat	weights[5][9]={
			{0},
			{0},
			{0},
			{0},
			{0},
	};

	gfloat	weights_out[3][6]={
			{0},
			{0},
			{0},
	};

	//打开笔画表
	lucykila_open_table(obj);

	//载入笔画训练集 （目前直接是静态数据）
	for(i = 0;i<5;i++)
		memcpy(obj->annhide[i].weights,weights[i],sizeof(gfloat)*9);

	for(i = 0 ; i < 3 ; i++)
		memcpy(obj->annoutput[i].weights,weights_out,sizeof(gfloat)*6);

	 gint trainset_input[5][8]={

	 };

	 gint trainset_output[5][3]={

	 };

	 //训练
	ibus_handwrite_recog_lucykila_train(obj,trainset_input,trainset_output);
}

static void ibus_handwrite_recog_lucykila_destory(GObject*obj)
{
	IbusHandwriteRecogLucyKila * thisobj = IBUS_HANDWRITE_RECOG_LUCYKILA(obj);
	g_string_free(thisobj->input, TRUE);
	munmap(thisobj->start_ptr, thisobj->maped_size);

	IBUS_HANDWRITE_RECOG_LUCYKILA_GET_CLASS(obj)->parentdestroy((GObject*)obj);
}

static void ibus_handwrite_recog_lucykila_class_init(
		IbusHandwriteRecogLucyKilaClass* klass)
{
	IbusHandwriteRecogClass * parent = (IbusHandwriteRecogClass*) (klass);

	parent->domatch = ibus_handwrite_recog_lucykila_domatch;

	//TODO
	parent->change_stroke = ibus_handwrite_recog_change_stroke;

	klass->parentdestroy = G_OBJECT_CLASS(klass)->finalize ;

	G_OBJECT_CLASS(klass)->finalize = ibus_handwrite_recog_lucykila_destory;

}

GType ibus_handwrite_recog_lucykila_get_type(void)
{
	static const GTypeInfo type_info =
	{ sizeof(IbusHandwriteRecogLucyKilaClass), (GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) ibus_handwrite_recog_lucykila_class_init, NULL,
			NULL, sizeof(IbusHandwriteRecogLucyKila), 0,
			(GInstanceInitFunc) ibus_handwrite_recog_lucykila_init, };

	static GType type = 0;

	if (type == 0)
	{
		type = g_type_register_static(G_TYPE_IBUS_HANDWRITE_RECOG,
				"IbusHandwriteRecog_LucyKila", &type_info, 0);

	}
	return type;
}

static char *
nextline(char * ptr)
{
	while (*ptr != '\n')
		++ptr;
	//  *ptr = 0;
	return *ptr ? ++ptr : NULL;
}

static gint
mysort(gconstpointer a, gconstpointer b)
{
  MATCHED * pa ,  *pb;
  pa = (MATCHED*) a;
  pb = (MATCHED*) b;
//  g_printf("match sort %s %s\n",pa->hanzi,pb->hanzi);

  return (strlen(pa->code) ) - (strlen(pb->code) ) ;
}

static int lucykila_open_table(IbusHandwriteRecogLucyKila*obj)
{
	struct stat state;
	char * ptr;
	const int max_length = 64; // 绝对够的，不够你找偶
	char *preserve, *ptr2;
	char * p;
	int preservesize;

	//打开表
	int f = open(tablefile, O_RDONLY);
	if (f < 0)
		return -1;

	fstat(f, &state);
	//映射进来
	preserve = ptr = (char*) mmap(0, state.st_size, PROT_WRITE | PROT_READ,
			MAP_PRIVATE, f, 0);
	if (!ptr)
	{
		close(f);
		return -1;
	}
	close(f);
	//优化数据文件，其实就是使得每一行都一样长

	preservesize = 1024 * 1024;

	//预先申请 1 M 内存，不够了再说
	ptr2 = obj->start_ptr = mmap(0, preservesize, PROT_WRITE | PROT_READ,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

	if (!preserve)
	{
		munmap(preserve, state.st_size);
		return -1;
	}

	obj->items_count = 0;

	//进入循环，一行一行的扫描 :)
	while ((ptr = nextline(ptr)) && ((ptr - preserve) < state.st_size))
	{
		memcpy(ptr2, ptr, 64); //直接拷贝过去就可以了
		nextline(ptr2)[-1] = 0;
		ptr2 += 64;
		obj->items_count++;
	}

	munmap(preserve, state.st_size);

	obj->maped_size = preservesize;
	obj->table_size = obj->items_count*64;
	return 0;
}
