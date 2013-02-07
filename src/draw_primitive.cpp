#include <math.h>

#include <boost/array.hpp>

#include <vector>

#include "asserts.hpp"
#include "color_utils.hpp"
#include "draw_primitive.hpp"
#include "geometry.hpp"

namespace graphics
{

namespace
{

typedef boost::array<GLfloat, 2> FPoint;

class arrow_primitive : public draw_primitive
{
public:
	explicit arrow_primitive(const variant& v);

private:

	void handle_draw() const;

	variant get_value(const std::string& key) const;
	void set_value(const std::string& key, const variant& value);

	void set_points(const variant& points);

	void curve(const FPoint& p1, const FPoint& p2, const FPoint& p3, std::vector<FPoint>* out) const;

	std::vector<FPoint> points_;
	GLfloat granularity_;
	int arrow_head_length_;
	GLfloat arrow_head_width_;
	graphics::color color_;
	int fade_in_length_;

	GLfloat width_base_, width_head_;
};

arrow_primitive::arrow_primitive(const variant& v)
  : granularity_(v["granularity"].as_decimal(decimal(0.005)).as_float()),
    arrow_head_length_(v["arrow_head_length"].as_int(10)),
    arrow_head_width_(v["arrow_head_width"].as_decimal(decimal(2.0)).as_float()),
	fade_in_length_(v["fade_in_length"].as_int(50)),
	width_base_(v["width_base"].as_decimal(decimal(12.0)).as_float()),
	width_head_(v["width_head"].as_decimal(decimal(5.0)).as_float())
{
	if(v.has_key("color")) {
		color_ = color(v["color"]);
	} else {
		color_ = color(200, 0, 0, 255);
	}

	set_points(v["points"]);
}

void arrow_primitive::handle_draw() const
{
	if(points_.size() < 3) {
		return;
	}

	std::vector<FPoint> path;

	for(int n = 1; n < points_.size()-1; ++n) {
		curve(points_[n-1], points_[n], points_[n+1], &path);
	}

	const GLfloat PathLength = path.size()-1;

	std::vector<FPoint> left_path, right_path;
	for(int n = 0; n < path.size()-1; ++n) {
		const FPoint& p = path[n];
		const FPoint& next = path[n+1];

		FPoint direction;
		for(int m = 0; m != 2; ++m) {
			direction[m] = next[m] - p[m];
		}

		const GLfloat vector_length = sqrt(direction[0]*direction[0] + direction[1]*direction[1]);
		if(vector_length == 0.0) {
			continue;
		}

		FPoint unit_direction;
		for(int m = 0; m != 2; ++m) {
			unit_direction[m] = direction[m]/vector_length;
		}
		
		FPoint normal_direction_left, normal_direction_right;
		normal_direction_left[0] = -unit_direction[1];
		normal_direction_left[1] = unit_direction[0];
		normal_direction_right[0] = unit_direction[1];
		normal_direction_right[1] = -unit_direction[0];

		const GLfloat ratio = n/PathLength;

		GLfloat arrow_width = width_base_ - (width_base_-width_head_)*ratio;

		const int time_until_end = path.size()-2 - n;
		if(time_until_end < arrow_head_length_) {
			arrow_width = arrow_head_width_*time_until_end;
		}

		FPoint left, right;
		for(int m = 0; m != 2; ++m) {
			left[m] = p[m] + normal_direction_left[m]*arrow_width;
			right[m] = p[m] + normal_direction_right[m]*arrow_width;
		}

		left_path.push_back(left);
		right_path.push_back(right);
	}

	std::vector<GLfloat> varray;
	std::vector<unsigned char> carray;
	for(int n = 0; n != left_path.size(); ++n) {
		varray.push_back(left_path[n][0]);
		varray.push_back(left_path[n][1]);
		varray.push_back(right_path[n][0]);
		varray.push_back(right_path[n][1]);

		for(int m = 0; m != 2; ++m) {
			carray.push_back(color_.r());
			carray.push_back(color_.g());
			carray.push_back(color_.b());
			if(n < fade_in_length_) {
				carray.push_back(int((GLfloat(color_.a())*GLfloat(n)*(255.0/GLfloat(fade_in_length_)))/255.0));
			} else {
				carray.push_back(color_.a());
			}
		}
	}

#if !defined(USE_GLES2)
	glEnableClientState(GL_COLOR_ARRAY);
	glDisable(GL_TEXTURE_2D);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	glVertexPointer(2, GL_FLOAT, 0, &varray[0]);
	glColorPointer(4, GL_UNSIGNED_BYTE, 0, &carray[0]);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, varray.size()/2);

	glDisableClientState(GL_COLOR_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glEnable(GL_TEXTURE_2D);
#else
	gles2::manager gles2_manager(gles2::get_simple_col_shader());
	gles2::active_shader()->shader()->vertex_array(2, GL_FLOAT, GL_FALSE, 0, &varray[0]);
	gles2::active_shader()->shader()->color_array(4, GL_UNSIGNED_BYTE, GL_TRUE, 0, &carray[0]);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, varray.size()/2);
#endif
}

variant arrow_primitive::get_value(const std::string& key) const
{
	ASSERT_LOG(false, "ILLEGAL KEY IN ARROW: " << key);
	return variant();
}

void arrow_primitive::set_value(const std::string& key, const variant& value)
{
	if(key == "points") {
		set_points(value);
	}

	ASSERT_LOG(false, "ILLEGAL KEY IN ARROW: " << key);
}

void arrow_primitive::set_points(const variant& points)
{
	ASSERT_LOG(points.is_list(), "arrow points is not a list: " << points.debug_location());

	for(int n = 0; n != points.num_elements(); ++n) {
		variant p = points[n];
		ASSERT_LOG(p.is_list() && p.num_elements() == 2, "arrow points in invalid format: " << points.debug_location());
		FPoint point;
		point[0] = p[0].as_int();
		point[1] = p[1].as_int();
		points_.push_back(point);
	}
}

void arrow_primitive::curve(const FPoint& p0, const FPoint& p1, const FPoint& p2, std::vector<FPoint>* out) const
{
	for(float t = 0.0; t < 1.0 - granularity_; t += granularity_) {
		FPoint p;
		for(int n = 0; n != 2; ++n) {
			//formula for a bezier curve.
			p[n] = (1-t)*(1-t)*p0[n] + 2*(1-t)*t*p1[n] + t*t*p2[n];
		}

		out->push_back(p);
	}
}

}

draw_primitive_ptr draw_primitive::create(const variant& v)
{
	const std::string type = v["type"].as_string();
	if(type == "arrow") {
		return draw_primitive_ptr(new arrow_primitive(v));
	}

	ASSERT_LOG(false, "UNKNOWN DRAW PRIMITIVE TYPE: " << v["type"].as_string());
	return draw_primitive_ptr();
}

void draw_primitive::draw() const
{
	handle_draw();
}

}
