// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

use std::fmt;
use std::mem::size_of;
use std::slice::from_raw_parts;

use error::{ArrowError, Result};
use serde_json::Value;

/// Arrow data type
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum DataType {
    Boolean,
    Int8,
    Int16,
    Int32,
    Int64,
    UInt8,
    UInt16,
    UInt32,
    UInt64,
    Float16,
    Float32,
    Float64,
    Utf8,
    List(Box<DataType>),
    Struct(Vec<Field>),
}

/// Arrow struct/schema field
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Field {
    name: String,
    data_type: DataType,
    nullable: bool,
}

/// Primitive type (ints, floats, strings)
pub trait ArrowPrimitiveType: Send + Sync + Copy + PartialOrd + 'static {}

impl ArrowPrimitiveType for bool {}
impl ArrowPrimitiveType for u8 {}
impl ArrowPrimitiveType for u16 {}
impl ArrowPrimitiveType for u32 {}
impl ArrowPrimitiveType for u64 {}
impl ArrowPrimitiveType for i8 {}
impl ArrowPrimitiveType for i16 {}
impl ArrowPrimitiveType for i32 {}
impl ArrowPrimitiveType for i64 {}
impl ArrowPrimitiveType for f32 {}
impl ArrowPrimitiveType for f64 {}

pub trait ToByteSlice {
    /// Converts this instance into a byte slice.
    fn to_byte_slice(&self) -> &[u8];
}

impl<T> ToByteSlice for [T]
where
    T: ArrowPrimitiveType,
{
    fn to_byte_slice(&self) -> &[u8] {
        let raw_ptr = self.as_ptr() as *const T as *const u8;
        unsafe { from_raw_parts(raw_ptr, self.len() * size_of::<T>()) }
    }
}

impl<T> ToByteSlice for T
where
    T: ArrowPrimitiveType,
{
    fn to_byte_slice(&self) -> &[u8] {
        let raw_ptr = self as *const T as *const u8;
        unsafe { from_raw_parts(raw_ptr, size_of::<T>()) }
    }
}

impl DataType {
    /// Parse a data type from a JSON representation
    fn from(json: &Value) -> Result<DataType> {
        match *json {
            Value::Object(ref map) => match map.get("name") {
                Some(s) if s == "bool" => Ok(DataType::Boolean),
                Some(s) if s == "utf8" => Ok(DataType::Utf8),
                Some(s) if s == "floatingpoint" => match map.get("precision") {
                    Some(p) if p == "HALF" => Ok(DataType::Float16),
                    Some(p) if p == "SINGLE" => Ok(DataType::Float32),
                    Some(p) if p == "DOUBLE" => Ok(DataType::Float64),
                    _ => Err(ArrowError::ParseError(
                        "floatingpoint precision missing or invalid".to_string(),
                    )),
                },
                Some(s) if s == "int" => match map.get("isSigned") {
                    Some(&Value::Bool(true)) => match map.get("bitWidth") {
                        Some(&Value::Number(ref n)) => match n.as_u64() {
                            Some(8) => Ok(DataType::Int8),
                            Some(16) => Ok(DataType::Int16),
                            Some(32) => Ok(DataType::Int32),
                            Some(64) => Ok(DataType::Int32),
                            _ => Err(ArrowError::ParseError(
                                "int bitWidth missing or invalid".to_string(),
                            )),
                        },
                        _ => Err(ArrowError::ParseError(
                            "int bitWidth missing or invalid".to_string(),
                        )),
                    },
                    Some(&Value::Bool(false)) => match map.get("bitWidth") {
                        Some(&Value::Number(ref n)) => match n.as_u64() {
                            Some(8) => Ok(DataType::UInt8),
                            Some(16) => Ok(DataType::UInt16),
                            Some(32) => Ok(DataType::UInt32),
                            Some(64) => Ok(DataType::UInt64),
                            _ => Err(ArrowError::ParseError(
                                "int bitWidth missing or invalid".to_string(),
                            )),
                        },
                        _ => Err(ArrowError::ParseError(
                            "int bitWidth missing or invalid".to_string(),
                        )),
                    },
                    _ => Err(ArrowError::ParseError(
                        "int signed missing or invalid".to_string(),
                    )),
                },
                Some(other) => Err(ArrowError::ParseError(format!(
                    "invalid type name: {}",
                    other
                ))),
                None => match map.get("fields") {
                    Some(&Value::Array(ref fields_array)) => {
                        let fields = fields_array
                            .iter()
                            .map(|f| Field::from(f))
                            .collect::<Result<Vec<Field>>>();
                        Ok(DataType::Struct(fields?))
                    }
                    _ => Err(ArrowError::ParseError("empty type".to_string())),
                },
            },
            _ => Err(ArrowError::ParseError(
                "invalid json value type".to_string(),
            )),
        }
    }

    /// Generate a JSON representation of the data type
    pub fn to_json(&self) -> Value {
        match *self {
            DataType::Boolean => json!({"name": "bool"}),
            DataType::Int8 => json!({"name": "int", "bitWidth": 8, "isSigned": true}),
            DataType::Int16 => json!({"name": "int", "bitWidth": 16, "isSigned": true}),
            DataType::Int32 => json!({"name": "int", "bitWidth": 32, "isSigned": true}),
            DataType::Int64 => json!({"name": "int", "bitWidth": 64, "isSigned": true}),
            DataType::UInt8 => json!({"name": "int", "bitWidth": 8, "isSigned": false}),
            DataType::UInt16 => json!({"name": "int", "bitWidth": 16, "isSigned": false}),
            DataType::UInt32 => json!({"name": "int", "bitWidth": 32, "isSigned": false}),
            DataType::UInt64 => json!({"name": "int", "bitWidth": 64, "isSigned": false}),
            DataType::Float16 => json!({"name": "floatingpoint", "precision": "HALF"}),
            DataType::Float32 => json!({"name": "floatingpoint", "precision": "SINGLE"}),
            DataType::Float64 => json!({"name": "floatingpoint", "precision": "DOUBLE"}),
            DataType::Utf8 => json!({"name": "utf8"}),
            DataType::Struct(ref fields) => {
                let field_json_array =
                    Value::Array(fields.iter().map(|f| f.to_json()).collect::<Vec<Value>>());
                json!({ "fields": field_json_array })
            }
            DataType::List(ref t) => {
                let child_json = t.to_json();
                json!({ "name": "list", "children": child_json })
            }
        }
    }
}

impl Field {
    pub fn new(name: &str, data_type: DataType, nullable: bool) -> Self {
        Field {
            name: name.to_string(),
            data_type,
            nullable,
        }
    }

    pub fn name(&self) -> &String {
        &self.name
    }

    pub fn data_type(&self) -> &DataType {
        &self.data_type
    }

    pub fn is_nullable(&self) -> bool {
        self.nullable
    }

    /// Parse a field definition from a JSON representation
    pub fn from(json: &Value) -> Result<Self> {
        match *json {
            Value::Object(ref map) => {
                let name = match map.get("name") {
                    Some(&Value::String(ref name)) => name.to_string(),
                    _ => {
                        return Err(ArrowError::ParseError(
                            "Field missing 'name' attribute".to_string(),
                        ))
                    }
                };
                let nullable = match map.get("nullable") {
                    Some(&Value::Bool(b)) => b,
                    _ => {
                        return Err(ArrowError::ParseError(
                            "Field missing 'nullable' attribute".to_string(),
                        ))
                    }
                };
                let data_type = match map.get("type") {
                    Some(t) => DataType::from(t)?,
                    _ => {
                        return Err(ArrowError::ParseError(
                            "Field missing 'type' attribute".to_string(),
                        ))
                    }
                };
                Ok(Field {
                    name,
                    nullable,
                    data_type,
                })
            }
            _ => Err(ArrowError::ParseError(
                "Invalid json value type for field".to_string(),
            )),
        }
    }

    /// Generate a JSON representation of the field
    pub fn to_json(&self) -> Value {
        json!({
            "name": self.name,
            "nullable": self.nullable,
            "type": self.data_type.to_json(),
        })
    }

    pub fn to_string(&self) -> String {
        format!("{}: {:?}", self.name, self.data_type)
    }
}

impl fmt::Display for Field {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}: {:?}", self.name, self.data_type)
    }
}

/// Arrow Schema
#[derive(Debug, Clone)]
pub struct Schema {
    columns: Vec<Field>,
}

impl Schema {
    /// create an empty schema
    pub fn empty() -> Self {
        Schema { columns: vec![] }
    }

    pub fn new(columns: Vec<Field>) -> Self {
        Schema { columns }
    }

    pub fn columns(&self) -> &Vec<Field> {
        &self.columns
    }

    pub fn column(&self, i: usize) -> &Field {
        &self.columns[i]
    }

    /// look up a column by name and return a reference to the column along with it's index
    pub fn column_with_name(&self, name: &str) -> Option<(usize, &Field)> {
        self.columns
            .iter()
            .enumerate()
            .find(|&(_, c)| c.name == name)
    }
}

impl fmt::Display for Schema {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.write_str(
            &self
                .columns
                .iter()
                .map(|c| c.to_string())
                .collect::<Vec<String>>()
                .join(", "),
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json;

    #[test]
    fn create_struct_type() {
        let _person = DataType::Struct(vec![
            Field::new("first_name", DataType::Utf8, false),
            Field::new("last_name", DataType::Utf8, false),
            Field::new(
                "address",
                DataType::Struct(vec![
                    Field::new("street", DataType::Utf8, false),
                    Field::new("zip", DataType::UInt16, false),
                ]),
                false,
            ),
        ]);
    }

    #[test]
    fn struct_field_to_json() {
        let f = Field::new(
            "address",
            DataType::Struct(vec![
                Field::new("street", DataType::Utf8, false),
                Field::new("zip", DataType::UInt16, false),
            ]),
            false,
        );
        assert_eq!(
            "{\"name\":\"address\",\"nullable\":false,\"type\":{\"fields\":[\
            {\"name\":\"street\",\"nullable\":false,\"type\":{\"name\":\"utf8\"}},\
            {\"name\":\"zip\",\"nullable\":false,\"type\":{\"bitWidth\":16,\"isSigned\":false,\"name\":\"int\"}}]}}",
            f.to_json().to_string()
        );
    }

    #[test]
    fn primitive_field_to_json() {
        let f = Field::new("first_name", DataType::Utf8, false);
        assert_eq!(
            "{\"name\":\"first_name\",\"nullable\":false,\"type\":{\"name\":\"utf8\"}}",
            f.to_json().to_string()
        );
    }
    #[test]
    fn parse_struct_from_json() {
        let json = "{\"name\":\"address\",\"nullable\":false,\"type\":{\"fields\":[\
        {\"name\":\"street\",\"nullable\":false,\"type\":{\"name\":\"utf8\"}},\
        {\"name\":\"zip\",\"nullable\":false,\"type\":{\"bitWidth\":16,\"isSigned\":false,\"name\":\"int\"}}]}}";
        let value: Value = serde_json::from_str(json).unwrap();
        let dt = Field::from(&value).unwrap();

        let expected = Field::new(
            "address",
            DataType::Struct(vec![
                Field::new("street", DataType::Utf8, false),
                Field::new("zip", DataType::UInt16, false),
            ]),
            false,
        );

        assert_eq!(expected, dt);
    }

    #[test]
    fn parse_utf8_from_json() {
        let json = "{\"name\":\"utf8\"}";
        let value: Value = serde_json::from_str(json).unwrap();
        let dt = DataType::from(&value).unwrap();
        assert_eq!(DataType::Utf8, dt);
    }

    #[test]
    fn parse_int32_from_json() {
        let json = "{\"name\": \"int\", \"isSigned\": true, \"bitWidth\": 32}";
        let value: Value = serde_json::from_str(json).unwrap();
        let dt = DataType::from(&value).unwrap();
        assert_eq!(DataType::Int32, dt);
    }

    #[test]
    fn create_schema_string() {
        let _person = Schema::new(vec![
            Field::new("first_name", DataType::Utf8, false),
            Field::new("last_name", DataType::Utf8, false),
            Field::new(
                "address",
                DataType::Struct(vec![
                    Field::new("street", DataType::Utf8, false),
                    Field::new("zip", DataType::UInt16, false),
                ]),
                false,
            ),
        ]);
        assert_eq!(_person.to_string(), "first_name: Utf8, last_name: Utf8, address: Struct([Field { name: \"street\", data_type: Utf8, nullable: false }, Field { name: \"zip\", data_type: UInt16, nullable: false }])")
    }

    #[test]
    fn schema_field_accessors() {
        let _person = Schema::new(vec![
            Field::new("first_name", DataType::Utf8, false),
            Field::new("last_name", DataType::Utf8, false),
            Field::new(
                "address",
                DataType::Struct(vec![
                    Field::new("street", DataType::Utf8, false),
                    Field::new("zip", DataType::UInt16, false),
                ]),
                false,
            ),
        ]);

        // test schema accessors
        assert_eq!(_person.columns().len(), 3);

        // test field accessors
        assert_eq!(_person.columns()[0].name(), "first_name");
        assert_eq!(_person.columns()[0].data_type(), &DataType::Utf8);
        assert_eq!(_person.columns()[0].is_nullable(), false);
    }

}
