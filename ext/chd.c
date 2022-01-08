#include <ruby.h>
#include <ruby/io.h>
#include <libchdr/chd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/**
 * Document-class: CHD
 *
 * Accessing CHD MAME file.
 */

/**
 * Document-class: CHD::Error
 *
 * Generic error raised by CHD.
 */

/**
 * Document-class: CHD::NotSupportedError
 *
 * An operation is not supported.
 *
 * It is mapped to the following libchdr error: `CHDERR_NOT_SUPPORTED`.
 */

/**
 * Document-class: CHD::IOError
 *
 * Error that happens when reading/writing data from/to the CHD file.
 * 
 * It is mapped to the following libchdr errors: 
 * `CHDERR_READ_ERROR`, `CHDERR_WRITE_ERROR`, `CHDERR_CODEC_ERROR`,
 * `CHDERR_HUNK_OUT_OF_RANGE`, `CHDERR_DECOMPRESSION_ERROR`,
 * `CHDERR_COMPRESSION_ERROR`.
 */

/**
 * Document-class: CHD::DataError
 *
 * Some retrieved data are not valid.
 *
 * It is mapped to the following libchdr errors: 
 * `CHDERR_INVALID_FILE`, `CHDERR_INVALID_DATA`. 
 */

/**
 * Document-class: CHD::NotFoundError
 *
 * The requested file doesn't exist.
 *
 * It is mapped to the following libchdr error: 
 * `CHDERR_FILE_NOT_FOUND`.
 */

/**
 * Document-class: CHD::NotWritableError
 *
 * The requested file is not writable.
 *
 * It is mapped to the following libchdr error: 
 * `CHDERR_FILE_NOT_WRITABLE`.
 */

/**
 * Document-class: CHD::UnsupportedError
 *
 * The file version/format is not supported.
 *
 * It is mapped to the following libchdr errors: 
 * `CHDERR_UNSUPPORTED_VERSION`, `CHDERR_UNSUPPORTED_FORMAT`.
 */

/**
 * Document-class: CHD::ParentRequiredError
 *
 * The a parent file is required by this CHD, as it only contains 
 * partial data.
 *
 * It is mapped to the following libchdr error: 
 * `CHDERR_REQUIRES_PARENT`.
 */

/**
 * Document-class: CHD::ParentInvalidError
 *
 * The provided parent file doesn't match the expected one
 * (digest hash differs).
 *
 * It is mapped to the following libchdr error: 
 * `CHDERR_INVALID_PARENT`.
 */



#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) \
    (sizeof(arr) / sizeof((arr)[0]) \
     + sizeof(typeof(int[1 - 2 * \
           !!__builtin_types_compatible_p(typeof(arr), \
                 typeof(&arr[0]))])) * 0)
#endif

#ifndef CHD_METATADATA_BUFFER_MAXSIZE
#define CHD_METATADATA_BUFFER_MAXSIZE 256
#endif


#if   SIZEOF_INT == SIZEOF_INT32_T
#if   SIZEOF_INT == SIZEOF_VALUE
#define VALUE_TO_UINT32(value) RB_NUM2UINT(value)
#else
#define VALUE_TO_UINT32(value) RB_FIX2UINT(value)
#endif
#elif SIZEOF_LONG == SIZEOF_INT32_T
#if   SIZEOF_LONG == SIZEOF_VALUE
#define VALUE_TO_UINT32(value) RB_NUM2ULONG(value)
#else
#define VALUE_TO_UINT32(value) RB_FIX2ULONG(value)
#endif
#else
#error "unable to establish conversion from VALUE to uint32_t"
#endif

#define chd_rb_get_typeddata(chd, obj)					\
    TypedData_Get_Struct(obj, struct chd_rb_data, &chd_data_type, chd)


struct chd_rb_data {
#define CHD_RB_DATA_INITIALIZED  0x01
#define CHD_RB_DATA_OPENED       0x02
#define CHD_RB_DATA_PRECACHED    0x04
          int         flags;
          chd_file   *file;
    const chd_header *header;
          uint8_t    *cached_hunk;
          int         cached_hunkidx;
          int         units_per_hunk;
    struct {
	VALUE header;
    } value;
};

static void chd_rb_data_type_free(void *data) {
    struct chd_rb_data *chd = data;
    if (chd->file) {
	chd_close(chd->file);
    }
    if (chd->cached_hunk) {
	free(chd->cached_hunk);
    }
    free(data);
}
static size_t chd_rb_data_type_size(const void *data) {
    const struct chd_rb_data *chd = data;
    size_t size             = sizeof(struct chd_rb_data);

    if (chd->cached_hunk)
	size += chd->header->hunkbytes;

    return size;
}
static void chd_rb_data_type_mark(void *data) {
    struct chd_rb_data *chd = data;
    rb_gc_mark_locations((VALUE *)(&chd->value),
			 (VALUE *)(((char*)(&chd->value) + sizeof(chd->value))));
}

static rb_data_type_t chd_data_type = {
    .wrap_struct_name = "chd",
    .function         = { .dmark = chd_rb_data_type_mark,
	                  .dfree = chd_rb_data_type_free,
			  .dsize = chd_rb_data_type_size, },
    .data             = NULL,
    .flags            = RUBY_TYPED_FREE_IMMEDIATELY,
};


static VALUE cCHD                        = Qundef;
static VALUE eCHDError                   = Qundef;
static VALUE eCHDNotSupportedError       = Qundef;
static VALUE eCHDIOError                 = Qundef;
static VALUE eCHDDataError               = Qundef;
static VALUE eCHDNotFoundError           = Qundef;
static VALUE eCHDNotWritableError        = Qundef;
static VALUE eCHDUnsupportedError        = Qundef;
static VALUE eCHDParentRequiredError     = Qundef;
static VALUE eCHDParentInvalidError      = Qundef;

static ID id_parent;
static ID id_version;
static ID id_compression;
static ID id_md5;
static ID id_sha1;
static ID id_sha1_raw;
static ID id_hunk_bytes;
static ID id_hunk_count;
static ID id_unit_bytes;
static ID id_unit_count;
static ID id_logical_bytes;


static VALUE chd_m_close(VALUE self);



static VALUE
chd_rb_alloc(VALUE klass)
{
    struct chd_rb_data *chd;
    VALUE               obj = TypedData_Make_Struct(cCHD, struct chd_rb_data,
						    &chd_data_type, chd);
    chd->value.header = Qnil;
    return obj;
}


static void
chd_rb_ensure_initialized(struct chd_rb_data *chd)
{
    if (! (chd->flags & CHD_RB_DATA_INITIALIZED)) {
	rb_bug("uninitialized instance");
    }
}

static void
chd_rb_ensure_opened(struct chd_rb_data *chd)
{
    if (! (chd->flags & CHD_RB_DATA_OPENED)) {
	rb_raise(eCHDError, "closed");
    }
}

static void
chd_rb_raise_if_error(chd_error err) {
    switch(err) {
    /* No error */
    case CHDERR_NONE:
	return;
	
    /* Out of memory */
    case CHDERR_OUT_OF_MEMORY:
	rb_raise(rb_eNoMemError, "out of memory (libchdr)");

    /* Argument Error */
    case CHDERR_INVALID_PARAMETER:
	rb_raise(rb_eArgError, "invalid parameter");
	
    /* Invalid */
    case CHDERR_INVALID_FILE:
    case CHDERR_INVALID_DATA:
	rb_raise(eCHDDataError, "invalid data");
	
    /* File */
    case CHDERR_FILE_NOT_FOUND:
	rb_raise(eCHDNotFoundError, "file not found");	
    case CHDERR_FILE_NOT_WRITEABLE:
	rb_raise(eCHDNotWritableError, "CHD is read-only");

    /* Unsupported */
    case CHDERR_UNSUPPORTED_VERSION:
    case CHDERR_UNSUPPORTED_FORMAT:
	rb_raise(eCHDUnsupportedError, "%s", chd_error_string(err));
	
    /* Parent required */
    case CHDERR_REQUIRES_PARENT:
	rb_raise(eCHDParentRequiredError, "parent CHD is required");
    case CHDERR_INVALID_PARENT:
	rb_raise(eCHDParentInvalidError, "invalid parent (checksum mismatch)");

    /* I/O */
    case CHDERR_READ_ERROR:
    case CHDERR_WRITE_ERROR:
    case CHDERR_CODEC_ERROR:
    case CHDERR_HUNK_OUT_OF_RANGE:
    case CHDERR_DECOMPRESSION_ERROR:
    case CHDERR_COMPRESSION_ERROR:
	rb_raise(eCHDIOError, "%s", chd_error_string(err));

    /* Not Supported */
    case CHDERR_NOT_SUPPORTED:
	rb_raise(eCHDNotSupportedError, "%s", chd_error_string(err));

    /* Should be hidden to user */
    case CHDERR_METADATA_NOT_FOUND:
	rb_bug("the <%s> should have been hidden", chd_error_string(err));

    /* Not used in libchdr code (as of 2022-01-01) */
    case CHDERR_INVALID_METADATA:
    case CHDERR_INVALID_METADATA_SIZE:
    case CHDERR_INVALID_STATE:
    case CHDERR_CANT_CREATE_FILE:
    case CHDERR_CANT_VERIFY:
    case CHDERR_VERIFY_INCOMPLETE:
    case CHDERR_OPERATION_PENDING:
    case CHDERR_NO_ASYNC_OPERATION:
    case CHDERR_NO_INTERFACE:
	rb_bug("the <%s> is not handled", chd_error_string(err));
	       
    /* Not defined in libchdr code (as of 2022-01-01) */
    default:
	rb_bug("the <%s> is unknown", chd_error_string(err));
    }
}


static VALUE
chd_rb_header(const chd_header *header) {
#define get_chd_hash(buffer, bytes)					\
    rb_str_freeze(rb_str_new((char *)(buffer), bytes))

    VALUE hdr                = rb_hash_new();
    VALUE parent             = rb_hash_new();
    
    rb_hash_aset(hdr, ID2SYM(id_version),      ULONG2NUM(header->version));
    rb_hash_aset(hdr, ID2SYM(id_hunk_bytes),   ULONG2NUM(header->hunkbytes));
    rb_hash_aset(hdr, ID2SYM(id_hunk_count),   ULONG2NUM(header->totalhunks));
    rb_hash_aset(hdr, ID2SYM(id_unit_bytes),   ULONG2NUM(header->unitbytes));
    rb_hash_aset(hdr, ID2SYM(id_unit_count),   ULONG2NUM(header->unitcount));
    rb_hash_aset(hdr, ID2SYM(id_logical_bytes),ULONG2NUM(header->logicalbytes));
    
    if (header->version >= 3) {
    	rb_hash_aset(hdr, ID2SYM(id_sha1),
		     get_chd_hash(header->sha1, CHD_SHA1_BYTES));
	if (header->flags & CHDFLAGS_HAS_PARENT) {
	    rb_hash_aset(parent, ID2SYM(id_sha1),
			 get_chd_hash(header->parentsha1, CHD_SHA1_BYTES));
	}
    }

    if (header->version <= 3) {
	rb_hash_aset(hdr, ID2SYM(id_md5),
		     get_chd_hash(header->md5, CHD_MD5_BYTES));
	if (header->flags & CHDFLAGS_HAS_PARENT) {
	    rb_hash_aset(parent, ID2SYM(id_md5),
			 get_chd_hash(header->parentmd5, CHD_MD5_BYTES));
	}
    }

    if (header->version >= 4) {
	rb_hash_aset(hdr, ID2SYM(id_sha1_raw),
		     get_chd_hash(header->rawsha1, CHD_SHA1_BYTES));
    }

    if (header->version >= 5) {
	VALUE compression = rb_ary_new();
	for (int i = 0 ; i < ARRAY_SIZE(header->compression) ; i++) {
	    if (header->compression[i] == CHD_CODEC_NONE)
		continue;

	    char str[sizeof(uint32_t)];
	    rb_integer_pack(ULONG2NUM(header->compression[i]),
			    str, 1, sizeof(uint32_t),
			    0, INTEGER_PACK_BIG_ENDIAN);
	    rb_ary_push(compression, rb_str_new(str, sizeof(str)));
	}
	rb_hash_aset(hdr, ID2SYM(id_compression), rb_ary_freeze(compression));
    }

    if (! RHASH_EMPTY_P(parent)) {
	rb_hash_aset(hdr, ID2SYM(id_parent), rb_hash_freeze(parent));
    }
    
    return rb_hash_freeze(hdr);
#undef get_chd_hash
}


/**
 * (see CHD#initialize)
 */
static VALUE
chd_s_new(int argc, VALUE *argv, VALUE klass)
{
    return rb_class_new_instance_kw(argc, argv, klass, RB_PASS_CALLED_KEYWORDS);
}


/**
 * Open a CHD file.
 *
 * With no associated block {CHD.open} is synonym for {CHD.new}.
 * If the optional code block is given, it will be passed the opened CHD file
 * as an argument and the CHD object will automatically be closed when
 * the block terminates. The value of the block will be returned.
 *
 * @note Only the read-only mode ({RDONLY}) is currently supported.
 *
 * @overload open(file, mode=RDONLY, parent: nil)
 *   @param file   [String, IO] path-string or open IO on the CHD file
 *   @param mode   [Integer]    opening mode ({RDONLY} or {RDWR})
 *   @param parent [String, IO] path-string or open IO on the CHD parent file.
 * 
 * @yield [chd] the opened CHD file
 *
 * @example
 *   CHD.open('file.chd') do |chd|
 *     puts chd.metadata
 *   end
 */
static VALUE
chd_s_open(int argc, VALUE *argv, VALUE klass)
{
    VALUE chd;

    chd = rb_class_new_instance_kw(argc, argv, klass, RB_PASS_CALLED_KEYWORDS);

    if (rb_block_given_p()) {
        return rb_ensure(rb_yield, chd, chd_m_close, chd);
    }

    return chd;
}


/**
 * Retrieve the header from a CHD file.
 *
 * For a detailed description of the header, see {#header}.
 * 
 * @param filename [String] path to CHD file
 *
 * @return [Hash{Symbol => Object}]
 */
static VALUE
chd_s_header(VALUE klass, VALUE filename)
{
    chd_header header;
    chd_error  err;

    err = chd_read_header(StringValueCStr(filename), &header);
    chd_rb_raise_if_error(err);    

    return chd_rb_header(&header);
}


/**
 * Create a new access to a CHD file.
 *
 * @note Only the read-only mode ({RDONLY}) is currently supported.
 *
 * @overload initialize(file, mode=RDONLY, parent: nil)
 *   @param file   [String, IO] path-string or open IO on the CHD file
 *   @param mode   [Integer]    opening mode ({RDONLY} or {RDWR})
 *   @param parent [String, IO] path-string or open IO on the CHD parent file.
 *
 * @return [CHD]
 */
static VALUE
chd_m_initialize(int argc, VALUE *argv, VALUE self)
{
    VALUE file, mode, opts;
    ID    kwargs_id[1] = { id_parent };
    VALUE kwargs   [1];
    
    // Retrieve typed data
    struct chd_rb_data *chd;
    chd_rb_get_typeddata(chd, self);

    // Sanity check
    if (chd->flags & CHD_RB_DATA_INITIALIZED) {
	rb_warn("%"PRIsVALUE" refusing to initialize same instance twice",
		rb_obj_as_string(cCHD));
	return Qnil;
    }

    // Retrieve arguments
    rb_scan_args_kw(RB_SCAN_ARGS_LAST_HASH_KEYWORDS, argc, argv, "11:",
		    &file, &mode, &opts);
    rb_get_kwargs(opts, kwargs_id, 0, 2, kwargs);

    // If mode not specified, default to read-only
    if (NIL_P(mode)) {
	mode = INT2FIX(CHD_OPEN_READ);
    }
    
    // If given retrieve parent chd file
    chd_file *parent = NULL;
    if ((kwargs[0] != Qundef) && (kwargs[0] != Qnil)) {
	if (! RTEST(rb_obj_is_kind_of(kwargs[0], cCHD))) {
	    rb_raise(rb_eArgError, "parent must be a kind of %"PRIsVALUE,
		     rb_obj_as_string(cCHD));
	}
	struct chd_rb_data *chd_parent;
	chd_rb_get_typeddata(chd_parent, self);
	parent = chd_parent->file;
    }

    // Open CHD
    chd_error err = CHDERR_NONE;
    if (RTEST(rb_obj_is_kind_of(file, rb_cIO))) {
        rb_io_t *fptr;
        GetOpenFile(file, fptr);

	err = chd_open_file(rb_io_stdio_file(fptr),
			    FIX2INT(mode), parent, &chd->file);
    } else {
	err = chd_open(StringValueCStr(file),
		       FIX2INT(mode), parent, &chd->file);
    }
    chd_rb_raise_if_error(err);    

    // Retrieve header and hunkbytes
    chd->header         = chd_get_header(chd->file);
    chd->units_per_hunk = chd->header->hunkbytes / chd->header->unitbytes;
    if (chd->header->hunkbytes % chd->header->unitbytes) {
	chd_close(chd->file);
	rb_raise(eCHDDataError, "CHD hunk is not a multiple of unit");
    }

    // Allocate cache
    chd->cached_hunk    = malloc(chd->header->hunkbytes);
    chd->cached_hunkidx = -1;
    if (chd->cached_hunk == NULL) {
	chd_close(chd->file);
	rb_raise(rb_eNoMemError, "out of memory (hunk cache)");
    }
    
    // Mark as initialized and opened
    chd->flags = CHD_RB_DATA_INITIALIZED | CHD_RB_DATA_OPENED;
    
    return Qnil;
}


/**
 * Pre-cache the whole CHD in memory.
 *
 * @note
 *  * It is not necessary to enable pre-cache just to improved
 *    consecutive partial-read as the current hunk is always cached.
 *  * Once enabled, there is no way to remove the cache.
 *
 * @return [self]
 */
static VALUE
chd_m_precache(VALUE self) {
    // Retrieve typed data
    struct chd_rb_data *chd;
    chd_rb_get_typeddata(chd, self);
    chd_rb_ensure_initialized(chd);
    chd_rb_ensure_opened(chd);

    chd_error err = chd_precache(chd->file);
    chd_rb_raise_if_error(err);
    chd->flags |= CHD_RB_DATA_PRECACHED;

    return self;
}


/**
 * Has the CHD been pre-cached?
 */
static VALUE
chd_m_precached_p(VALUE self) {
    // Retrieve typed data
    struct chd_rb_data *chd;
    chd_rb_get_typeddata(chd, self);
    chd_rb_ensure_initialized(chd);
    chd_rb_ensure_opened(chd);

    return (chd->flags & CHD_RB_DATA_PRECACHED) ? Qtrue : Qfalse;
}


/**
 * Return the CHD header. 
 * 
 * The header contains information about:
 *  * version
 *  *  compression used
 *  *  digest (sha1 or md5) for the file and the parent 
 *  * hunk and unit (size and count)
 *
 * @return [Hash{Symbol => Object}]
 */
static VALUE
chd_m_header(VALUE self) {
    // Retrieve typed data
    struct chd_rb_data *chd;
    chd_rb_get_typeddata(chd, self);
    chd_rb_ensure_initialized(chd);
    chd_rb_ensure_opened(chd);
    
    if (NIL_P(chd->value.header)) {
	chd->value.header = rb_obj_freeze(chd_rb_header(chd->header));
    }

    return chd->value.header;
}


/**
 * Retrieve a single metadata.
 *
 * If specified the metadata tag is a 4-character symbol, some commonly used
 * are `:GDDD`, `:IDNT`, `:'KEY '`, `:'CIS '`, `:CHTR`, `:CHT2`, `:CHGD`,
 * `:AVAV`, `:AVLD`. Note the use of single-quote to include a white-space
 * in some of the tag.
 *
 * @overload get_metadata(index=0, tag=nil)
 *   @param index [Integer]      index from which to lookup for metadata
 *   @param tag   [Symbol, nil]  tag of the metadata to lookup
 *                               (using nil as a wildcard)
 *
 * @return [Array(String, Integer, Symbol)]
 * @return [nil] if do metadata found
 */
static VALUE
chd_m_get_metadata(int argc, VALUE *argv, VALUE self)
{
    VALUE tag, index;

    // Retrieve arguments
    rb_scan_args_kw(RB_SCAN_ARGS_LAST_HASH_KEYWORDS, argc, argv, "02",
		    &index, &tag);

    if (! NIL_P(index)) {
	rb_check_type(index, T_FIXNUM);	
    }
    
    if (! NIL_P(tag)) {
	rb_warn("%"PRIsVALUE" refusing to initialize same instance twice",
		rb_obj_as_string(tag));
	rb_check_type(tag, T_SYMBOL);
	tag = rb_sym2str(tag);
	if (RSTRING_LEN(tag) != 4) {
	    rb_raise(rb_eArgError, "tag must be a 4-char symbol");
	}
    }

    // Retrieve typed data
    struct chd_rb_data *chd;
    chd_rb_get_typeddata(chd, self);
    chd_rb_ensure_initialized(chd);
    chd_rb_ensure_opened(chd);

    // Perform query
    char     buffer[CHD_METATADATA_BUFFER_MAXSIZE] = { 0 };
    uint32_t buflen                                = sizeof(buffer);
    uint32_t resultlen                             = 0;
    uint32_t resulttag                             = 0;
    uint8_t  resultflags                           = 0;    
    uint32_t searchindex                           = 0;
    uint32_t searchtag                             = CHDMETATAG_WILDCARD;

    if (! NIL_P(index)) {
	searchindex = FIX2INT(index);
    }

    if (! NIL_P(tag)) {
	char *tag_str       = RSTRING_PTR(tag);
	char *searchtag_str = (char *)&searchtag;
	searchtag_str[0] = tag_str[3];
	searchtag_str[1] = tag_str[2];
	searchtag_str[2] = tag_str[1];
	searchtag_str[3] = tag_str[0];
    }
    
    chd_error err;
    err = chd_get_metadata(chd->file,
			   searchtag, searchindex,
			   buffer, buflen,
			   &resultlen, &resulttag, &resultflags);

    // return nil on not found, otherwise raise exception
    if (err == CHDERR_METADATA_NOT_FOUND)
	return Qnil;
    chd_rb_raise_if_error(err);    

    // Sanity check on buffer length
    if (resultlen > buflen) {
	rb_bug("decoding metadata buffer size (%d) is too small (got: %d)",
	       (int)sizeof(buffer), resultlen);
    }

    // Assume it's ascii 8-bit text encoded, remove last null-char
    if ((resultlen > 0) &&
	(buffer[resultlen-1] == '\0') &&
	(strchr(buffer, '\0') == &buffer[resultlen-1])) {
	resultlen -= 1;
    }

    // Returns result
    char str[sizeof(uint32_t)];
    rb_integer_pack(ULONG2NUM(resulttag), str, 1, sizeof(uint32_t), 0,
		    INTEGER_PACK_BIG_ENDIAN);

    VALUE res[] = { rb_str_new(buffer, resultlen),
	            INT2FIX(resultflags),
		    rb_to_symbol(rb_str_new(str, sizeof(str)))
                  };
    
    return rb_ary_new_from_values(ARRAY_SIZE(res), res);
}


/**
 * Retrieve all the metadata.
 *
 * @return [Array<Array(String, Integer, Symbol)>]
 */
static VALUE
chd_m_metadata(VALUE self) {
    VALUE list = rb_ary_new();
    
    for (int i = 0 ; ; i++) {
	VALUE md  = chd_m_get_metadata(1, (VALUE []) { INT2FIX(i) }, self);
	if (NIL_P(md))
	    break;
	rb_ary_push(list, md);
    }
    return list;
}


/**
 * Read a CHD hunk.
 *
 * @param idx [Integer] hunk index (start at 0)
 *
 * @raise [RangeError] if the requested hunk doesn't exists
 *
 * @return [String]
 */
static VALUE
chd_m_read_hunk(VALUE self, VALUE idx) {
    // Retrieve typed data
    struct chd_rb_data *chd;
    chd_rb_get_typeddata(chd, self);
    chd_rb_ensure_initialized(chd);
    chd_rb_ensure_opened(chd);

    uint32_t hunkidx = VALUE_TO_UINT32(idx);
    if ((hunkidx < 0) || (hunkidx >= chd->header->totalhunks)) {
	rb_raise(rb_eRangeError, "hunk index (%d) is out of range (%d..%d)",
		 hunkidx, 0, chd->header->totalhunks - 1);
    }

    VALUE strdata = rb_str_buf_new(chd->header->hunkbytes);
    char *buffer  = RSTRING_PTR(strdata);
    
    chd_error err = chd_read(chd->file, hunkidx, buffer);
    chd_rb_raise_if_error(err);

    rb_str_set_len(strdata, chd->header->hunkbytes);
    return strdata;
}


/**
 * Read a CHD unit.
 *
 * @param idx [Integer] unit index (start at 0)
 *
 * @raise [RangeError] if the requested unit doesn't exists
 *
 * @return [String]
 */
static VALUE
chd_m_read_unit(VALUE self, VALUE idx) {
    // Retrieve typed data
    struct chd_rb_data *chd;
    chd_rb_get_typeddata(chd, self);
    chd_rb_ensure_initialized(chd);
    chd_rb_ensure_opened(chd);

    const uint32_t hunkbytes  = chd->header->hunkbytes;
    const uint32_t unitbytes  = chd->header->unitbytes;
    const uint32_t unitidx    = VALUE_TO_UINT32(idx);
    const uint32_t hunkidx    = unitidx / chd->units_per_hunk;
    const size_t   offset     = (unitidx % chd->units_per_hunk) * unitbytes;
	  
    if (hunkidx != chd->cached_hunkidx) {
	chd_error err = chd_read(chd->file, hunkidx, chd->cached_hunk);
	chd->cached_hunkidx = (err == CHDERR_NONE) ? hunkidx : -1;
	chd_rb_raise_if_error(err);
    }

    return rb_str_new((char *) &chd->cached_hunk[offset], unitbytes);
}


/**
 * Read bytes of data.
 *
 * @param offset  [Integer] offset from which reading bytes start
 * @param size    [Integer] number of bytes to read
 *
 * @raise [IOError] if the requested data is not available
 *
 * @return [String]
 */
static VALUE
chd_m_read_bytes(VALUE self, VALUE offset, VALUE size) {
    // Retrieve typed data
    struct chd_rb_data *chd;
    chd_rb_get_typeddata(chd, self);
    chd_rb_ensure_initialized(chd);
    chd_rb_ensure_opened(chd);
    
    const uint32_t  _offset       = VALUE_TO_UINT32(offset);
    const uint32_t  _size         = VALUE_TO_UINT32(size);
    const uint32_t  hunkbytes     = chd->header->hunkbytes;
    const uint32_t  hunkidx_first = _offset               / hunkbytes;
    const uint32_t  hunkidx_last  = (_offset + _size - 1) / hunkbytes;
    const VALUE     strdata       = rb_str_buf_new(_size);
          char     *buffer        = RSTRING_PTR(strdata);

    for (uint32_t hunkidx = hunkidx_first; hunkidx <= hunkidx_last; hunkidx++) {
	uint32_t startoffs = (hunkidx == hunkidx_first)
	                   ? (_offset % hunkbytes)
	                   : 0;
	uint32_t endoffs   = (hunkidx == hunkidx_last)
	                   ? ((_offset + _size - 1) % hunkbytes)
	                   : (hunkbytes - 1);
	size_t   chunksize = endoffs + 1 - startoffs;
	
	// if it's a full block, just read directly from disk
	// (unless it's the cached hunk)
	if ((startoffs == 0                   ) &&
	    (endoffs   == (hunkbytes - 1)     ) &&
	    (hunkidx   != chd->cached_hunkidx)) {
	    chd_error err = chd_read(chd->file, hunkidx, buffer);
	    chd_rb_raise_if_error(err);
	}
	// otherwise, read from the cache
	// (and fill the cache if necessary)
	else {
	    if (hunkidx != chd->cached_hunkidx) {
		chd_error err = chd_read(chd->file, hunkidx, chd->cached_hunk);
		chd->cached_hunkidx = (err == CHDERR_NONE) ? hunkidx : -1;
		chd_rb_raise_if_error(err);
	    }
	    memcpy(buffer, &chd->cached_hunk[startoffs], chunksize);
	}
	
	buffer += chunksize;
    }

    rb_str_set_len(strdata, _size);
    return strdata;
}
    

/**
 * Close the file.
 *
 * @note Once closed, further operation on this object will result 
 *       in an {Error} exception, except for {#close} that will be a no-op.
 *
 * @return [nil]
 */
static VALUE
chd_m_close(VALUE self) {
    // Retrieve typed data
    struct chd_rb_data *chd;
    chd_rb_get_typeddata(chd, self);
    chd_rb_ensure_initialized(chd);
	
    // If opened
    if (chd->flags & CHD_RB_DATA_OPENED) {
	chd_close(chd->file);
	chd->file         = NULL;
	chd->header       = NULL;
	chd->value.header = Qnil;
	chd->flags       &= ~(CHD_RB_DATA_OPENED | CHD_RB_DATA_PRECACHED);
    }
    
    return Qnil;
}


/**
 * Is the file closed?
 */
static VALUE
chd_m_closed_p(VALUE self) {
    // Retrieve typed data
    struct chd_rb_data *chd;
    chd_rb_get_typeddata(chd, self);
    chd_rb_ensure_initialized(chd);

    return (chd->flags & CHD_RB_DATA_OPENED) ? Qfalse : Qtrue;
}


/**
 * Returns version number.
 *
 * @return [String]
 */
static VALUE
chd_m_version(VALUE self) {
    return rb_hash_lookup(chd_m_header(self), ID2SYM(id_version));
}

/**
 * Number of bytes in a hunk.
 *
 * @return [Integer]
 */
static VALUE
chd_m_hunk_bytes(VALUE self) {
    return rb_hash_lookup(chd_m_header(self), ID2SYM(id_hunk_bytes));
}

/**
 * Number of hunks.
 *
 * @return [Integer]
 */
static VALUE
chd_m_hunk_count(VALUE self) {
    return rb_hash_lookup(chd_m_header(self), ID2SYM(id_hunk_count));
}

/**
 * Number of bytes in a unit
 *
 * @return [Integer]
 */
static VALUE
chd_m_unit_bytes(VALUE self) {
    return rb_hash_lookup(chd_m_header(self), ID2SYM(id_unit_bytes));
}

/**
 * Number of units.
 *
 * @return [Integer]
 */
static VALUE
chd_m_unit_count(VALUE self) {
    return rb_hash_lookup(chd_m_header(self), ID2SYM(id_unit_count));
}

void Init_core(void) {
    /* Main classes */
    cCHD      = rb_define_class("CHD", rb_cObject);
    eCHDError = rb_define_class_under(cCHD, "Error", rb_eStandardError);

    /* Sub errors */
    eCHDIOError                 = rb_define_class_under(cCHD,
				      "IOError",                 eCHDError);
    eCHDNotSupportedError       = rb_define_class_under(cCHD,
			  	      "NotSupportedError",       eCHDError);
    eCHDDataError               = rb_define_class_under(cCHD,
				      "DataError",               eCHDError);
    eCHDNotFoundError           = rb_define_class_under(cCHD,
				      "NotFoundError",           eCHDError);
    eCHDNotWritableError        = rb_define_class_under(cCHD,
				      "NotWritableError",        eCHDError);
    eCHDUnsupportedError        = rb_define_class_under(cCHD,
				      "UnsupportedError",        eCHDError);
    eCHDParentRequiredError     = rb_define_class_under(cCHD,
				      "ParentRequiredError",     eCHDError);
    eCHDParentInvalidError      = rb_define_class_under(cCHD,
				      "ParentInvalidError",      eCHDError);

    /* ID */
    id_parent        = rb_intern("parent");
    id_version       = rb_intern("version");
    id_compression   = rb_intern("compression");
    id_md5           = rb_intern("md5");
    id_sha1          = rb_intern("sha1");
    id_sha1_raw      = rb_intern("sha1_raw");
    id_hunk_bytes    = rb_intern("hunk_bytes");
    id_hunk_count    = rb_intern("hunk_count");
    id_unit_bytes    = rb_intern("unit_bytes");
    id_unit_count    = rb_intern("unit_count");
    id_logical_bytes = rb_intern("logical_bytes");
    
    /* Constants */
    /* 1: Read-only mode for opening CHD file. */
    rb_define_const(cCHD, "RDONLY", INT2FIX(CHD_OPEN_READ));
    /* 2: Read-write mode for opening CHD file. */
    rb_define_const(cCHD, "RDWR",   INT2FIX(CHD_OPEN_READWRITE));
    /* 0x01: Indicates that data is checksumed */
    rb_define_const(cCHD, "METADATA_FLAG_CHECKSUM", INT2FIX(CHD_MDFLAGS_CHECKSUM));

    /* Definitions */
    rb_define_alloc_func(cCHD, chd_rb_alloc);
    rb_define_singleton_method(cCHD, "new", chd_s_new, -1);
    rb_define_singleton_method(cCHD, "header", chd_s_header, 1);
    rb_define_singleton_method(cCHD, "open", chd_s_open, -1);
    rb_define_method(cCHD, "initialize", chd_m_initialize, -1);
    rb_define_method(cCHD, "precache", chd_m_precache, 0);
    rb_define_method(cCHD, "precached?", chd_m_precached_p, 0);
    rb_define_method(cCHD, "header", chd_m_header, 0);
    rb_define_method(cCHD, "get_metadata", chd_m_get_metadata, -1);
    rb_define_method(cCHD, "metadata", chd_m_metadata, 0);
    rb_define_method(cCHD, "read_hunk", chd_m_read_hunk, 1);
    rb_define_method(cCHD, "read_unit", chd_m_read_unit, 1);
    rb_define_method(cCHD, "read_bytes", chd_m_read_bytes, 2);
    rb_define_method(cCHD, "close", chd_m_close, 0);
    rb_define_method(cCHD, "closed?", chd_m_closed_p, 0);
    rb_define_method(cCHD, "version", chd_m_version, 0);
    rb_define_method(cCHD, "hunk_bytes", chd_m_hunk_bytes, 0);
    rb_define_method(cCHD, "hunk_count", chd_m_hunk_count, 0);
    rb_define_method(cCHD, "unit_bytes", chd_m_unit_bytes, 0);
    rb_define_method(cCHD, "unit_count", chd_m_unit_count, 0);
}


